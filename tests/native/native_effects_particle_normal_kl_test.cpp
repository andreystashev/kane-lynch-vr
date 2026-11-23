// Original pinned SpriteDraw full type7 writer and camera-relative binder
// in a private relocated PE. External material lookup/D3D calls are stubs;
// the native WORLD*VIEW, source records, allocation callers, corners and
// Unlock paths execute their actual bytes. No game entry, GPU or window.
#include "native_hud_kl.cpp"
#include "native_effects_kl.cpp"
#include <fstream>
#include <iterator>
#include <cstdlib>
namespace NativeCameraKL {
EffectsView testView;bool validView=true;
std::uintptr_t CinematicCamera(){return 0;}
bool CinematicPanelActive(){return false;}
bool ReadEffectsView(EffectsView& output){output=testView;return validView;}
bool ReadReflectionBillboardReference(std::uintptr_t,NativeCameraMath::CameraPose&){return false;}
bool ReadReflectionScatter(ReflectionTargetView&,float (&)[8]){return false;}
}
namespace {
using namespace NativeCameraMath;
using Vertex=NativeEffectsKLMath::Vertex;
unsigned checks=0,allocated=0;
bool traceActive=false;
bool TraceActive(){return traceActive;}
std::uintptr_t traceEntry=0,traceState=0;
std::uint32_t traceStamp=1000;
NativeEffectsKL::VisualSourceDiagnostic capturedVisual;
bool scopeOverlay=false;
[[maybe_unused]] bool ScopeOverlay(){return scopeOverlay;}
std::uintptr_t fixtureImage=0;
LONG WINAPI Fault(EXCEPTION_POINTERS* info){const auto* c=info->ContextRecord;
    std::fprintf(stderr,"CPU fixture fault code=%08lx EIP=%08lx RVA=%08lx ECX=%08lx EDX=%08lx ESP=%08lx\n",info->ExceptionRecord->ExceptionCode,c->Eip,
        static_cast<unsigned long>(c->Eip-fixtureImage),c->Ecx,c->Edx,c->Esp);std::fflush(stderr);ExitProcess(1);}
std::vector<Vertex> mappedVertices,observed;
std::vector<unsigned char> queuedRenderer(0xA8000),queuedMetadata(2048*12);
std::vector<Vertex> queuedVertices(2048*4);
std::vector<float> queuedDepths(2048);
std::vector<std::uint32_t> queuedIndices(2048);
void* __cdecl QueueRenderer(){return queuedRenderer.data();}
void* __cdecl NativeCopy(void* destination,const void* source,std::size_t count){return std::memcpy(destination,source,count);}
struct Device {void** table;};struct Resource {void** table;};
void* deviceTable[16]{},*resourceTable[16]{};
Device device{deviceTable};Resource resource{resourceTable};
NativeHudKL::NativeBufferControl bufferControl;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uint32_t Ptr(const void* p){return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));}
ULONG STDMETHODCALLTYPE DeviceRelease(void*){return 1;}
HRESULT STDMETHODCALLTYPE GetDevice(void*,IDirect3DDevice9** output){*output=reinterpret_cast<IDirect3DDevice9*>(&device);return S_OK;}
HRESULT STDMETHODCALLTYPE GetDesc(void*,D3DVERTEXBUFFER_DESC* output){*output={};output->Type=D3DRTYPE_VERTEXBUFFER;output->Size=bufferControl.capacity;return S_OK;}
void* __thiscall Lock(void*,UINT count,UINT stride,UINT* first){
    Check(stride==24&&count<=mappedVertices.size(),"actual native lock requests bounded stride24 vertices");
    allocated=count;*first=0;bufferControl.end=count*stride;bufferControl.mapped=Ptr(mappedVertices.data());return mappedVertices.data();
}
void __thiscall Unlock(void*){observed.insert(observed.end(),mappedVertices.begin(),mappedVertices.begin()+allocated);}
unsigned __fastcall NoEntryContext(void*,void*){return 0;}
unsigned __fastcall BufferPosition(void*,void*,unsigned,unsigned){return 0;}
void __fastcall BindStream(void*,void*,unsigned,unsigned){}
void __cdecl Draw(unsigned,unsigned,unsigned){
    if(traceActive){NativeEffectsKL::VisualSourceDiagnostic next;
        if(NativeEffectsKL::SnapshotVisualSource(traceEntry,traceState,next))capturedVisual=next;}
}
void Jump(unsigned char* at,const void* target){at[0]=0xE9;Put(std::uintptr_t(at+1),std::int32_t(std::uintptr_t(target)-(std::uintptr_t(at)+5)));}
CameraPose Eye(float yaw,float pitch,float roll){
    const Vec3 f{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    const Vec3 right{std::cos(yaw),0,-std::sin(yaw)},up=Cross(f,right);
    return {{f,Subtract(Scale(up,std::cos(roll)),Scale(right,std::sin(roll))),Add(Scale(right,std::cos(roll)),Scale(up,std::sin(roll)))},{80,140,-30}};
}
Vec3 World(const CameraPose& eye,const Vertex& v){return Add(eye.position,Add(Add(Scale(eye.basis.right,v.x),Scale(eye.basis.up,v.y)),Scale(eye.basis.forward,v.z)));}
}
int main(int argc,char** argv){
    if (argc < 2) {
        std::puts("Pass a supported game executable for the native fixture.");
        return 2;
    }
    const char* path = argv[1];
    char digest[65]{};wchar_t widePath[32768]{};
    Check(MultiByteToWideChar(CP_UTF8,0,path,-1,widePath,32768)>0,"native EXE path decodes as UTF-8");
    if(!HashFile(widePath,digest)){std::puts("SKIP pinned EXE unavailable");return 0;}
    Check(IsSupportedExecutableHash(digest),"actual writer fixture uses pinned supported EXE");
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    const auto size=nt->OptionalHeader.SizeOfImage;
    auto* pe=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(pe&&Ptr(pe)<0x80000000u,"private low-address native CPU image");
    fixtureImage=std::uintptr_t(pe);AddVectoredExceptionHandler(1,Fault);
    std::memcpy(pe,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];Check(s.VirtualAddress+s.SizeOfRawData<=size&&s.PointerToRawData+s.SizeOfRawData<=bytes.size(),"PE section bounds");std::memcpy(pe+s.VirtualAddress,bytes.data()+s.PointerToRawData,s.SizeOfRawData);}
    const auto originalRead=[&](std::uintptr_t at,void* out,std::size_t count){if(at<0x400000u||at-0x400000u+count>size)return false;std::memcpy(out,pe+at-0x400000u,count);return true;};
    Check(NativeEffectsKLImage::Verify(0x400000u,originalRead),"actual world effect writer flags lock anchor and record signatures verified");
    pe[0x997C9]^=1;Check(!NativeEffectsKLImage::Verify(0x400000u,originalRead),"changed native type6 record step is rejected");pe[0x997C9]^=1;
    const auto delta=Ptr(pe)-0x400000u;
    // Supported EXE has no relocations. Only the absolute operands executed
    // by these original routines are relocated in this private fixture.
    for(unsigned off:{0x96a79u,0x96a81u,0x990afu,0x990e1u,0x990fau,0x9912eu,0x9914fu,0x99170u,0x99180u,0x99195u,0x991acu,0x991ecu,0x99241u,0x99257u,0x992c2u,0x992c9u,0x993ecu,0x99405u,0x994dau,0x995aeu,0x995cau,0x995e2u,0x995efu,0x99673u,0x99857u,0x998c7u,0x99a56u,0x99aa1u,0x99abdu,0x99ac5u,0x99ad2u,0x99af1u,0x99b03u,0x99b22u,0x99c21u,0x99c2bu,0x99c45u,0x99c67u,0x99d4eu,0x99d5cu,0x99db5u,0x99e3eu,0x99e4eu,0x99ed2u,0x99edcu,0x99f0cu,0x99f47u,0x71da1u,0x71dd5u,0x100816u,0x10081eu,0x100826u,0x10082eu,0x100836u,0x10083eu,0x99960u,0x99989u,0x9a175u,0x9a17au}){
        std::uint32_t value=0;std::memcpy(&value,pe+off,4);Check(value>=0x400000u&&value<0x400000u+size,"explicit native absolute operand");Put(std::uintptr_t(pe+off),value+delta);}
    for(unsigned i=0;i<7;++i){auto* target=reinterpret_cast<std::uint32_t*>(pe+0x9A1F4+i*4);*target+=delta;}
    // Material lookup is outside the writer contract. Retain the native entry
    // prologue/locals, then join at the original WORLD*VIEW and type dispatch.
    Check(pe[0x990DD]==0x75&&pe[0x990DE]==0x2C,"native material-lookup branch boundary");
    Jump(pe+0x990DD,pe+0x9923F);pe[0x990E2]=0x90;
    Jump(pe+0x8E150,reinterpret_cast<const void*>(&NativeHudKL::VertexLockHook));
    Jump(pe+0x8E350,reinterpret_cast<const void*>(&NativeHudKL::BufferUnlockHook));
    Jump(pe+0x8E290,reinterpret_cast<const void*>(&BufferPosition));Jump(pe+0x8DD60,reinterpret_cast<const void*>(&BindStream));
    Jump(pe+0x984F0,reinterpret_cast<const void*>(&Draw));
    Jump(pe+0x63610,reinterpret_cast<const void*>(&QueueRenderer));
    Jump(pe+0x4BA9CE,reinterpret_cast<const void*>(&NativeCopy));
    Put(std::uintptr_t(pe+0x7A3FC),Ptr(pe+0x63370C)); // native queue optional-flag global
    pe[0x4BABA4]=0xD9;pe[0x4BABA5]=0xFA;pe[0x4BABA6]=0xC3; // pure x87 sqrt intrinsic
    Put(std::uintptr_t(pe+0x540EAC+0x20),Ptr(reinterpret_cast<const void*>(&NoEntryContext)));

    pe[0x4BAB98]=0xD9;pe[0x4BAB99]=0xFF;pe[0x4BAB9A]=0xC3;
    pe[0x4BAB9E]=0xD9;pe[0x4BAB9F]=0xFE;pe[0x4BABA0]=0xC3;
    const unsigned char ftol[]={0x83,0xEC,8,0xDF,0x3C,0x24,0x8B,0x04,0x24,0x8B,0x54,0x24,4,0x83,0xC4,8,0xC3};
    std::memcpy(pe+0x4BA9E0,ftol,sizeof(ftol));
    NativeHudKL::image=std::uintptr_t(pe);NativeEffectsKL::nativeImage=std::uintptr_t(pe);
    NativeHudKL::originalVertexLock=Lock;NativeHudKL::originalBufferUnlock=Unlock;
    NativeHudKL::originalSpriteDraw=reinterpret_cast<NativeHudKL::SpriteDraw>(pe+0x990A0);
    NativeHudKL::SetMappedVertexFilter(NativeEffectsKL::FilterMapped);NativeEffectsKL::Configure(NativeCameraKL::ReadEffectsView);
    deviceTable[2]=reinterpret_cast<void*>(&DeviceRelease);resourceTable[3]=reinterpret_cast<void*>(&GetDevice);resourceTable[13]=reinterpret_cast<void*>(&GetDesc);
    mappedVertices.resize(2050*4);bufferControl.resource=Ptr(&resource);bufferControl.capacity=unsigned(mappedVertices.size()*sizeof(Vertex));
    alignas(16) unsigned char state[0x500]{},entry[0x150]{},binder[0x20]{},material[0x40]{},materialOuter[0x10]{},manager[0x30]{},geometry[0x60]{},nativeDevice[0x30]{};
    Put(std::uintptr_t(pe+0x786670),Ptr(manager));Put(std::uintptr_t(manager+0x1C),Ptr(&bufferControl));
    Put(std::uintptr_t(pe+0x786658),Ptr(nativeDevice));Put(std::uintptr_t(state+0x10),Ptr(nativeDevice));
    Put(std::uintptr_t(binder),Ptr(pe+0x53FB88));Put(std::uintptr_t(binder+4),Ptr(materialOuter));Put(std::uintptr_t(materialOuter+4),Ptr(material));
    Put(std::uintptr_t(binder+0xC),Ptr(entry));Put(std::uintptr_t(entry),Ptr(pe+0x540EAC));
    const CameraPose root{{{.78f,0,1.04f},{0,1.1f,0},{.56f,0,-.42f}},{300,210,900}};
    Put(std::uintptr_t(entry+0x10),root);Put(std::uintptr_t(entry+0x40),root);
    auto& view=NativeCameraKL::testView;view.state=std::uintptr_t(state);view.reference=Eye(.2f,.1f,-.1f);view.centre=view.reference.position;view.worldKnown=true;
    using Bind=void(__thiscall*)(void*,void*,void*);const auto bind=reinterpret_cast<Bind>(pe+0x71DA0);
    auto* queue=queuedRenderer.data()+0xA7E2C;
    Put(std::uintptr_t(queue),Ptr(pe+0x540D3C));queue[4]=4;
    Put(std::uintptr_t(queue+8),std::uint32_t(24));Put(std::uintptr_t(queue+0x18),std::uint32_t(2048));
    Put(std::uintptr_t(queue+0x28),Ptr(queuedVertices.data()));Put(std::uintptr_t(queue+0x2C),Ptr(queuedDepths.data()));
    Put(std::uintptr_t(queue+0x30),Ptr(queuedIndices.data()));Put(std::uintptr_t(queue+0x3C),Ptr(queuedMetadata.data()));
    auto* deferredJoin=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(deferredJoin!=nullptr,"private native deferred route join");
    const unsigned char forceDeferred[]={0xC6,0x44,0x24,0x23,1};std::memcpy(deferredJoin,forceDeferred,sizeof(forceDeferred));
    Jump(deferredJoin+sizeof(forceDeferred),pe+0x9923F);

    const auto run=[&](const CameraPose& eye,bool normal,bool filter,unsigned mode=0,bool deferredRoute=false){
        NativeHudKL::InvalidateDevice();observed.clear();capturedVisual={};entry[0xFC]=0;view.eye=eye;
        Jump(pe+0x990DD,deferredRoute?deferredJoin:pe+0x9923F);
        Put(std::uintptr_t(queue+0x10),std::uint32_t(0));
        CameraPose inverse;Check(NativeCameraKLMath::ViewFromWorld(eye,inverse),"native inverse view fixture");Put(std::uintptr_t(state+0xBC),inverse);
        bind(entry,state,nullptr);std::memcpy(view.world,state+0x8C,sizeof(view.world));
        unsigned char record[0x3C]{},normalData[0x34]{};std::uint32_t colours[128];for(auto& c:colours)c=0xFFFFFFFF;
        Put(std::uintptr_t(record),Vec3{0,0,400});Put(std::uintptr_t(record+0xC),Vec3{4,2,1});Put(std::uintptr_t(record+0x18),0.f);Put(std::uintptr_t(record+0x1C),100.f);
        Put(std::uintptr_t(record+0x20),.2f);Put(std::uintptr_t(record+0x24),30.f);
        Put(std::uintptr_t(normalData+4),Vec3{0,0,1});Put(std::uintptr_t(normalData+0x14),Vec3{.05f,.1f,.2f});Put(std::uintptr_t(normalData+0x24),Vec3{1,.8f,.3f});
        Put(std::uintptr_t(record+0x38),normal?Ptr(normalData):0);
        Put(std::uintptr_t(geometry),std::uint32_t(1));Put(std::uintptr_t(geometry+8),Ptr(record));
        geometry[0x30]=static_cast<unsigned char>(mode);Put(std::uintptr_t(geometry+0x28),1.f);Put(std::uintptr_t(geometry+0x34),std::uint32_t(1));Put(std::uintptr_t(geometry+0x44),Ptr(colours));
        for(unsigned slot=0;slot<2;++slot){Put(std::uintptr_t(entry+0x100+slot*4),Ptr(geometry));Put(std::uintptr_t(entry+0x114+slot*20),std::uint32_t(7));Put(std::uintptr_t(entry+0x130+slot*4),std::uint32_t(1000));}
        if(traceActive){const auto slot=*reinterpret_cast<std::uint32_t*>(pe+0x7678A4);Put(std::uintptr_t(pe+0x7678A0),slot);
            Put(std::uintptr_t(entry+0x130+slot*4),traceStamp);
            NativeHudKL::RecordSpriteProducer(std::uintptr_t(entry),manager,0x246810,0x8ABCDE01u,geometry,&root.basis,&root.position,19,0);}
        NativeHudKL::SetMappedVertexFilter(filter?NativeEffectsKL::FilterMapped:nullptr);
        std::uintptr_t spBefore=0,spAfter=0;
        __asm__ __volatile__("movl %%esp,%0":"=r"(spBefore));
        NativeHudKL::SpriteDrawHook(binder,nullptr,state);
        __asm__ __volatile__("movl %%esp,%0":"=r"(spAfter));
        Check(spBefore==spAfter,"actual native full writer retains exact caller stack and return");
        if(deferredRoute){std::uint32_t queued=0;std::memcpy(&queued,queue+0x10,4);
            Check(queued==1&&observed.empty(),"actual deferred Type7 bypasses original Unlock/draw and appends one real native queue quad");
            observed.assign(queuedVertices.begin(),queuedVertices.begin()+queued*4);}
        Check(observed.size()==4,"original type7 alive particle emits four native corners");return observed;
    };

    Check(NativeEffectsKLProducer::VerifyParticleNormalCall(std::uintptr_t(pe),[](std::uintptr_t at,void* out,std::size_t count){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,count,&got)&&got==count;}),"actual particle normal source/matrix/CALL bytes verified");
    pe[0x99A1D]^=1;
    Check(!NativeEffectsKL::InstallParticleNormal(std::uintptr_t(pe)),"changed shared-helper target rejects callsite hook");pe[0x99A1D]^=1;
    Check(MH_Initialize()==MH_OK,"real private MinHook initialized");
    Check(NativeEffectsKLProducer::VerifyParticleDeferredCall(std::uintptr_t(pe),[](std::uintptr_t at,void* out,std::size_t count){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,count,&got)&&got==count;}),"actual Type7 deferred static buffers and exact queue CALL verified");
    pe[0x9A185]^=1;Check(!NativeEffectsKL::InstallParticleDeferred(std::uintptr_t(pe)),"changed deferred shared-helper target fails closed");pe[0x9A185]^=1;
    Check(NativeEffectsKL::InstallParticleDeferred(std::uintptr_t(pe)),"actual MinHook installs exact deferred CALL adapter");
    Check(pe[0x9A189]==0xEB&&pe[0x9A18A]==0x4F,"deferred +5 native continuation untouched");
    Check(NativeEffectsKL::InstallParticleNormal(std::uintptr_t(pe)),"actual MinHook synchronizes exactly five CALL bytes with native return adapter");
    Check(pe[0x99A21]==0xD9&&pe[0x99A22]==0x44,"native normal-null branch and +5 continuation remain byte-exact");
    // The complete native writer itself requires an empty x87 stack; adding
    // a sentinel before that whole routine overflows its original peak use.
    // Prove the interior normal CALL adapter separately with live x87 data,
    // its original one-stack-argument fastcall ABI and the real native helper.
    auto* abi=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(abi!=nullptr,"private exact normal-call ABI code allocated");
    const unsigned char caller[]={0xFF,0x74,0x24,4,0xE8,0,0,0,0,0xC2,4,0};std::memcpy(abi,caller,sizeof(caller));
    Put(std::uintptr_t(abi+5),std::int32_t(std::uintptr_t(pe+0x2E780)-std::uintptr_t(abi+9)));
    Check(MH_CreateHook(abi+4,reinterpret_cast<void*>(&KL1ParticleNormalCallHook),nullptr)==MH_OK&&MH_EnableHook(abi+4)==MH_OK,"real MinHook installs the same CALL adapter in a standalone ABI fixture");
    KL1ParticleNormalReturn=abi+9;
    using Normal=void(__fastcall*)(Vec3*,const Vec3*,const Basis*);
    const auto invokeNormal=reinterpret_cast<Normal>(abi);const Vec3 authored{.4f,.8f,-.2f};const Basis basis;
    for(bool active:{false,true}){
        NativeHudKL::particleDrawContext={};
        if(active){auto& c=NativeHudKL::particleDrawContext;c.sourceState=std::uintptr_t(state);c.spriteType=7;c.spriteDrawId=0xFF1234;c.particleMode=0;}
        Vec3 output;const double sentinel=123.25;double restored=0;unsigned short beforeControl=0,afterControl=0;
        __asm__ __volatile__("fnstcw %0\n\tfldl %1":"=m"(beforeControl):"m"(sentinel));
        invokeNormal(&output,&authored,&basis);
        __asm__ __volatile__("fstpl %0\n\tfnstcw %1":"=m"(restored),"=m"(afterControl));
        Check(restored==sentinel&&beforeControl==afterControl,"normal CALL wrapper preserves live x87 value and control in native and corrected branches");
    }
    NativeHudKL::particleDrawContext={};KL1ParticleNormalReturn=pe+0x99A21;
    Check(MH_DisableHook(abi+4)==MH_OK&&MH_RemoveHook(abi+4)==MH_OK,"standalone ABI hook restored safely");VirtualFree(abi,0,MEM_RELEASE);
    unsigned changedColour=0;
    for(unsigned slot:{0u,1u}){Put(std::uintptr_t(pe+0x7678A4),slot);
    for(bool normal:{false,true}){
        const auto baseline=run(view.reference,normal,true);
        for(const auto& eye:{Eye(-.8f,.4f,.3f),Eye(.9f,-.3f,-.4f)}){
            const auto before=NativeEffectsKL::Snapshot();
            const auto corrected=run(eye,normal,true),native=run(eye,normal,false);
            const auto after=NativeEffectsKL::Snapshot();
            for(unsigned i=0;i<4;++i){const auto a=World(view.reference,baseline[i]),c=World(eye,corrected[i]);
                Check(Dot(Subtract(a,c),Subtract(a,c))<.00001f,"real type7 offsets remain fixed world geometry under HMD yaw pitch roll");
                Check(corrected[i].colour==baseline[i].colour,"native lit particle corner RGBA stays byte-exact to authored reference view");
                Check(!std::memcmp(&corrected[i].u,&native[i].u,8),"actual native type7 animated UV bits are untouched");
                changedColour+=normal&&native[i].colour!=baseline[i].colour;
            }
            if(!normal)Check(after.particleNormalCalls==before.particleNormalCalls,"unlit particles bypass the native normal branch entirely");
            const auto aligned=run(eye,normal,true,1),alignedNative=run(eye,normal,false,1);
            Check(!std::memcmp(aligned.data(),alignedNative.data(),4*sizeof(Vertex)),"native velocity-aligned particle geometry RGBA and UV remain byte-exact");
        }
    }}
    Check(changedColour>0,"original native +38 branch actually reproduces HMD-dependent color on otherwise fixed corrected quads");
    const auto eye=Eye(.9f,.3f,-.2f);const auto native=run(eye,true,false);
    view.state=std::uintptr_t(state)+4;const auto foreign=run(eye,true,true);
    Check(!std::memcmp(native.data(),foreign.data(),4*sizeof(Vertex)),"foreign native state cannot borrow reference lighting or XYZ correction");view.state=std::uintptr_t(state);
    NativeCameraKL::validView=false;const auto inactive=run(eye,true,true);
    Check(!std::memcmp(native.data(),inactive.data(),4*sizeof(Vertex)),"inactive main camera leaves complete original native particle output");NativeCameraKL::validView=true;
    const auto corrected=run(eye,true,true);(void)corrected;
    const auto evidence=NativeEffectsKL::Snapshot();
    Check(evidence.particleNormalInstalled&&evidence.particleNormalApplied&&evidence.particleNormalAlignedPreserved&&evidence.particleNormalRejected,
        "automatic counters distinguish applied aligned preserved and foreign normal paths");
    Check(evidence.lastParticleNormalApplied.valid&&evidence.lastParticleNormalApplied.reason==0&&evidence.lastParticleNormalApplied.mode==0&&
        evidence.lastParticleNormalApplied.sourceState==std::uintptr_t(state)&&evidence.lastParticleNormalApplied.sourceEntry==std::uintptr_t(entry)&&
        evidence.lastParticleNormalApplied.sourceGeometry==std::uintptr_t(geometry)&&evidence.lastParticleNormalApplied.normalData&&evidence.lastParticleNormalApplied.drawScopeId,
        "bounded coherent evidence records native +38 lighting source mode entry geometry normal and draw identity");
    traceEntry=std::uintptr_t(entry);traceState=std::uintptr_t(state);view.frame=88;
    NativeEffectsKL::ConfigureVisualSourceDiagnostics(TraceActive);traceActive=true;
    const auto normalCallsBefore=NativeEffectsKL::Snapshot().particleNormalCalls;
    const auto unlit=run(eye,false,true);
    Check(NativeEffectsKL::Snapshot().particleNormalCalls==normalCallsBefore&&capturedVisual.valid&&capturedVisual.type==7&&
        capturedVisual.mode==0&&capturedVisual.liveQuads==1&&capturedVisual.allocatedQuads==1&&capturedVisual.recordStride==60,
        "unlit mode0 actual native writer produces geometry diagnostics even when normal helper never runs");
    Check(capturedVisual.recordsKnown&&capturedVisual.recordCount==1&&capturedVisual.rawRecords[0][14]==0&&
        capturedVisual.rawBeforeKnown&&capturedVisual.rawAfterKnown&&!std::memcmp(capturedVisual.rawQuadAfter,unlit.data(),96),
        "unlit source +38/null raw record and actual before/after live quad are captured without lighting assumptions");
    Check(capturedVisual.producer.valid&&capturedVisual.producer.callerRva==0x246810&&capturedVisual.producer.textureHandle==0x8ABCDE01u&&
        capturedVisual.viewKnown&&capturedVisual.frame==88&&!std::memcmp(capturedVisual.eye,&view.eye,sizeof(capturedVisual.eye))&&
        !std::memcmp(capturedVisual.reference,&view.reference,sizeof(capturedVisual.reference)),
        "actual unlit draw identifies emitter asset handle WORLD and complete eye/reference geometry truth");
    NativeEffectsKL::VisualSourceDiagnostic deferred;
    Check(!NativeEffectsKL::SnapshotVisualSource(traceEntry,traceState,deferred),"unlit deferred draw reports unavailable rather than stale sprite data");
    traceActive=false;NativeEffectsKL::ConfigureVisualSourceDiagnostics(nullptr);
    for(unsigned slot:{0u,1u}){Put(std::uintptr_t(pe+0x7678A4),slot);
        const auto baseline=run(view.reference,false,true,0,true);
        for(const auto& tracked:{Eye(-.8f,.4f,.3f),Eye(.9f,-.3f,-.4f)}){
            const auto before=NativeEffectsKL::Snapshot();const auto correctedDeferred=run(tracked,false,true,0,true);
            const auto correctedMetadata=queuedMetadata;const auto correctedDepth=queuedDepths[0];
            const auto after=NativeEffectsKL::Snapshot();const auto nativeDeferred=run(tracked,false,false,0,true);
            Check(after.particleDeferredCalls==before.particleDeferredCalls+1&&after.particleDeferredQuads==before.particleDeferredQuads+1&&
                after.particleWrites==before.particleWrites,"actual deferred route is corrected once without depending on mapped Unlock");
            bool differed=false;
            for(unsigned i=0;i<4;++i){const auto expected=World(view.reference,baseline[i]),actual=World(tracked,correctedDeferred[i]);
                Check(Dot(Subtract(expected,actual),Subtract(expected,actual))<.00001f,"actual deferred native world corners stable under HMD yaw pitch roll");
                Check(correctedDeferred[i].colour==nativeDeferred[i].colour&&!std::memcmp(&correctedDeferred[i].u,&nativeDeferred[i].u,8),"deferred native RGBA and animated UV unchanged");
                differed=differed||Dot(Subtract(World(tracked,nativeDeferred[i]),expected),Subtract(World(tracked,nativeDeferred[i]),expected))>.01f;}
            Check(differed,"unfixed native deferred route reproduces HMD-dependent plane rotation");
            Check(queuedDepths[0]==correctedDepth&&!std::memcmp(queuedMetadata.data(),correctedMetadata.data(),12),"real native queue depth/order/material metadata unchanged");
            const auto alignedDeferred=run(tracked,false,true,1,true),nativeAlignedDeferred=run(tracked,false,false,1,true);
            Check(!std::memcmp(alignedDeferred.data(),nativeAlignedDeferred.data(),4*sizeof(Vertex)),"deferred velocity-aligned emitter remains native");
        }
    }
    view.state=std::uintptr_t(state)+4;const auto foreignDeferred=run(eye,false,true,0,true),originalDeferred=run(eye,false,false,0,true);
    Check(!std::memcmp(foreignDeferred.data(),originalDeferred.data(),4*sizeof(Vertex)),"deferred foreign state cannot borrow main camera");view.state=std::uintptr_t(state);
    NativeCameraKL::validView=false;const auto inactiveDeferred=run(eye,false,true,0,true);
    Check(!std::memcmp(inactiveDeferred.data(),originalDeferred.data(),4*sizeof(Vertex)),"deferred inactive view stays native");NativeCameraKL::validView=true;
    const auto deferredStats=NativeEffectsKL::Snapshot();Check(deferredStats.particleDeferredInstalled&&deferredStats.particleDeferredQuads&&deferredStats.particleDeferredAlignedPreserved&&deferredStats.particleDeferredRejected,"deferred installed/applied/aligned/rejected automatic counters");
    // Reproduce the grenade symptom with the REAL expanded native particle:
    // look at the emitter from ninety degrees to the old reference camera.
    // The former fixed sheet is edge-on; a position-facing particle retains
    // its authored area, with either original native output route.
    const auto original=run(view.reference,false,false);Vec3 particleCentre{};
    for(const auto& v:original)particleCentre=Add(particleCentre,Scale(World(view.reference,v),.25f));
    auto side=view.reference;side.basis={view.reference.basis.right,view.reference.basis.up,Scale(view.reference.basis.forward,-1.f)};
    side.position=Subtract(particleCentre,Scale(side.basis.forward,1000.f));view.centre=side.position;
    const auto area=[](const std::vector<Vertex>& v){float a=0;for(unsigned i=0;i<4;++i){const auto& p=v[i];const auto& q=v[(i+1)%4];
        if(p.z<=0||q.z<=0)return 0.f;a+=(p.x*q.y-p.y*q.x)/(p.z*q.z);}return std::fabs(a)*.5f;};
    for(bool deferredRoute:{false,true})for(bool lit:{false,true}){
        const auto colourReference=run(view.reference,lit,true,0,deferredRoute);
        const auto facing=run(side,lit,true,0,deferredRoute);auto frozen=run(side,lit,false,0,deferredRoute);
        Check(NativeEffectsKLMath::MappedParticleQuads(frozen.data(),1,side,view.reference),"former frozen-plane policy remains an executable regression control");
        Check(area(facing)>.0001f&&area(frozen)<area(facing)*.0001f,"actual normal and deferred smoke keeps visible area where old reference plane collapses to stripes");
        Vec3 centre{};for(const auto& v:facing)centre=Add(centre,Scale(World(side,v),.25f));
        Check(Dot(Subtract(centre,particleCentre),Subtract(centre,particleCentre))<.0001f,"position-facing smoke preserves native WORLD particle centre");
        for(unsigned i=0;i<4;++i)Check(facing[i].colour==colourReference[i].colour&&!std::memcmp(&facing[i].u,&frozen[i].u,8),"spherical facing leaves accepted reference lighting and animated UV untouched");
    }
    view.centre=view.reference.position;
    auto* deferredAbi=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(deferredAbi!=nullptr,"private eight-argument deferred CALL ABI fixture");
    // Copy all eight original stack arguments in reverse order, then execute
    // the actual MinHook-owned CALL and RET32 on both sides of this bridge.
    const unsigned char pushArgument[]={0xFF,0x74,0x24,0x20};
    for(unsigned i=0;i<8;++i)std::memcpy(deferredAbi+i*4,pushArgument,4);
    deferredAbi[32]=0xE8;Put(std::uintptr_t(deferredAbi+33),std::int32_t(std::uintptr_t(pe+0x7A310)-std::uintptr_t(deferredAbi+37)));
    const unsigned char returnEight[]={0xC2,0x20,0};std::memcpy(deferredAbi+37,returnEight,3);
    Check(MH_CreateHook(deferredAbi+32,reinterpret_cast<void*>(&KL1ParticleDeferredCallHook),nullptr)==MH_OK&&MH_EnableHook(deferredAbi+32)==MH_OK,"same real deferred CALL detour installed on ABI bridge");
    KL1ParticleDeferredReturn=deferredAbi+37;
    const auto invokeDeferred=reinterpret_cast<NativeEffectsKL::ParticleDeferred>(deferredAbi);
    for(bool active:{false,true}){
        NativeHudKL::particleDrawContext={};if(active){auto& c=NativeHudKL::particleDrawContext;c.sourceState=std::uintptr_t(state);c.spriteType=7;c.spriteDrawId=0x1234567;c.particleMode=0;c.sourceEntry=std::uintptr_t(entry);c.sourceGeometry=std::uintptr_t(geometry);}
        Put(std::uintptr_t(queue+0x10),std::uint32_t(0));const double sentinel=172.625;double restored=0;unsigned short controlBefore=0,controlAfter=0;
        std::uintptr_t stackBefore=0,stackAfter=0;__asm__ __volatile__("movl %%esp,%0\n\tfnstcw %1\n\tfldl %2":"=r"(stackBefore),"=m"(controlBefore):"m"(sentinel));
        const auto returned=invokeDeferred(queue,pe+0x791400,reinterpret_cast<float*>(pe+0x789400),0x12F7,0x2345,0x89ABCDEF,5,1,.625f);
        __asm__ __volatile__("fstpl %0\n\tfnstcw %1\n\tmovl %%esp,%2":"=m"(restored),"=m"(controlAfter),"=r"(stackAfter));
        Check(stackBefore==stackAfter&&restored==sentinel&&controlBefore==controlAfter&&(returned&255)==1,"deferred CALL preserves native RET32, AL result, x87 stack/control");
        std::uint32_t metadata[3]{},technique=0;std::memcpy(metadata,queuedMetadata.data(),12);std::memcpy(&technique,queue+0xC,4);float order=0;std::memcpy(&order,metadata+2,4);
        Check(metadata[0]==0x52F70000&&metadata[1]==0x89ABCDEF&&order==.625f&&technique==0x2345,"all eight deferred native arguments retain exact material/sort/flags/count/order meaning");
    }
    NativeHudKL::particleDrawContext={};KL1ParticleDeferredReturn=pe+0x9A189;
    Check(MH_DisableHook(deferredAbi+32)==MH_OK&&MH_RemoveHook(deferredAbi+32)==MH_OK,"deferred ABI bridge safely restored");VirtualFree(deferredAbi,0,MEM_RELEASE);
    Check(MH_DisableHook(pe+0x9A184)==MH_OK&&MH_RemoveHook(pe+0x9A184)==MH_OK,"private deferred CALL restored cleanly");
    Check(MH_DisableHook(pe+0x99A1C)==MH_OK&&MH_RemoveHook(pe+0x99A1C)==MH_OK,"private callsite hook removed cleanly");MH_Uninitialize();
    Check(pe[0x99A1C]==0xE8,"original native shared-helper CALL restored after private proof");
    std::printf("Native full type7 normal/deferred/corner CPU: %u checks PASS; actual writers and MinHook callsites, no GPU.\n",checks);VirtualFree(deferredJoin,0,MEM_RELEASE);VirtualFree(pe,0,MEM_RELEASE);
}
