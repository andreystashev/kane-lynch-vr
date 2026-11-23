// Original pinned SpriteDraw type5/6 writer loops and camera-relative binder
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
CameraPose tracerReference;
std::uintptr_t tracerEntry=0,tracerGeometry=0;
bool TracerReference(std::uintptr_t entry,std::uintptr_t geometry,std::uintptr_t records,unsigned type,unsigned first,unsigned count,CameraPose& output){
    if(entry!=tracerEntry||geometry!=tracerGeometry||!records||type!=6||first||count!=1)return false;
    output=tracerReference;return true;
}
bool scopeOverlay=false;
bool ScopeOverlay(){return scopeOverlay;}
bool traceActive=false,producerMismatch=false;
bool TraceActive(){return traceActive;}
std::uintptr_t traceEntry=0,traceState=0;
std::uint32_t traceStamp=0;
NativeEffectsKL::VisualSourceDiagnostic traceCaptured;
std::uintptr_t fixtureImage=0;
LONG WINAPI Fault(EXCEPTION_POINTERS* info){const auto* c=info->ContextRecord;
    std::fprintf(stderr,"CPU fixture fault code=%08lx EIP=%08lx RVA=%08lx ECX=%08lx EDX=%08lx ESP=%08lx\n",info->ExceptionRecord->ExceptionCode,c->Eip,
        static_cast<unsigned long>(c->Eip-fixtureImage),c->Ecx,c->Edx,c->Esp);std::fflush(stderr);ExitProcess(1);}
std::vector<Vertex> mappedVertices,observed;
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
        if(NativeEffectsKL::SnapshotVisualSource(traceEntry,traceState,next))traceCaptured=next;}
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
    Check(NativeEffectsKLImage::Verify(0x400000u,originalRead),"actual world type5/6 flags lock anchor and record signatures verified");
    pe[0x997C9]^=1;Check(!NativeEffectsKLImage::Verify(0x400000u,originalRead),"changed native type6 record step is rejected");pe[0x997C9]^=1;
    const auto delta=Ptr(pe)-0x400000u;
    // Supported EXE has no relocations. Only the absolute operands executed
    // by these original routines are relocated in this private fixture.
    for(unsigned off:{0x990AFu,0x990E1u,0x990FAu,0x9912Eu,0x9914Fu,0x99170u,0x99180u,0x99195u,0x991ACu,0x991ECu,
        0x99241u,0x99257u,0x992C2u,0x992C9u,0x993ECu,0x99405u,0x994DAu,0x995AEu,0x995CAu,0x995E2u,0x995EFu,0x99673u,0x71DA1u,0x71DD5u}){
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
    pe[0x4BABA4]=0xD9;pe[0x4BABA5]=0xFA;pe[0x4BABA6]=0xC3; // pure x87 sqrt intrinsic
    Put(std::uintptr_t(pe+0x540EAC+0x20),Ptr(reinterpret_cast<const void*>(&NoEntryContext)));
    NativeHudKL::image=std::uintptr_t(pe);NativeEffectsKL::nativeImage=std::uintptr_t(pe);
    NativeHudKL::originalVertexLock=Lock;NativeHudKL::originalBufferUnlock=Unlock;
    NativeHudKL::originalSpriteDraw=reinterpret_cast<NativeHudKL::SpriteDraw>(pe+0x990A0);
    NativeHudKL::SetMappedVertexFilter(NativeEffectsKL::FilterMapped);NativeEffectsKL::Configure(NativeCameraKL::ReadEffectsView);
    deviceTable[2]=reinterpret_cast<void*>(&DeviceRelease);resourceTable[3]=reinterpret_cast<void*>(&GetDevice);resourceTable[13]=reinterpret_cast<void*>(&GetDesc);
    mappedVertices.resize(2050*4);bufferControl.resource=Ptr(&resource);bufferControl.capacity=unsigned(mappedVertices.size()*sizeof(Vertex));
    alignas(16) unsigned char state[0x500]{},entry[0x150]{},binder[0x20]{},material[0x40]{},materialOuter[0x10]{},manager[0x30]{},geometry[16]{},nativeDevice[0x30]{};
    Put(std::uintptr_t(pe+0x786670),Ptr(manager));Put(std::uintptr_t(manager+0x1C),Ptr(&bufferControl));
    Put(std::uintptr_t(pe+0x786658),Ptr(nativeDevice));Put(std::uintptr_t(state+0x10),Ptr(nativeDevice));
    Put(std::uintptr_t(binder),Ptr(pe+0x53FB88));Put(std::uintptr_t(binder+4),Ptr(materialOuter));Put(std::uintptr_t(materialOuter+4),Ptr(material));
    Put(std::uintptr_t(binder+0xC),Ptr(entry));Put(std::uintptr_t(entry),Ptr(pe+0x540EAC));
    const CameraPose root{{{.78f,0,1.04f},{0,1.1f,0},{.56f,0,-.42f}},{300,210,900}};
    Put(std::uintptr_t(entry+0x10),root);Put(std::uintptr_t(entry+0x40),root);
    auto& view=NativeCameraKL::testView;view.state=std::uintptr_t(state);view.reference=Eye(.2f,.1f,-.1f);view.worldKnown=true;
    using Bind=void(__thiscall*)(void*,void*,void*);const auto bind=reinterpret_cast<Bind>(pe+0x71DA0);
    const auto run=[&](unsigned type,unsigned count,const CameraPose& eye,bool relative,bool filter){
        NativeHudKL::InvalidateDevice();observed.clear();traceCaptured={};entry[0xFC]=relative?1:0;view.eye=eye;
        CameraPose inverse;Check(NativeCameraKLMath::ViewFromWorld(eye,inverse),"native inverse view fixture");Put(std::uintptr_t(state+0xBC),inverse);
        bind(entry,state,nullptr);std::memcpy(view.world,state+0x8C,sizeof(view.world));
        const unsigned stride=type==5?40:0x74;std::vector<unsigned char> records(std::size_t(count)*stride);
        for(unsigned q=0;q<count;++q){auto* record=records.data()+std::size_t(q)*stride;
            const Vec3 centre{float(q%11)*3,float(q%7)*2,400.f+float(q)*.5f};std::memcpy(record,&centre,sizeof(centre));
            for(unsigned u=0;u<(type==5?4u:8u);++u)Put(std::uintptr_t(record+0xC+u*4),float(u)*.1f);
            if(type==5){Put(std::uintptr_t(record+0x1C),20.f);Put(std::uintptr_t(record+0x20),30.f);Put(std::uintptr_t(record+0x24),std::uint32_t(0xF4A0B0C0));}
            else {const Vec3 offsets[]{{-20,-15,2},{-7,29,6},{14,18,-3},{45,-8,13}};
                for(unsigned i=0;i<4;++i){std::memcpy(record+0x2C+i*12,&offsets[i],12);Put(std::uintptr_t(record+0x64+i*4),std::uint32_t(0xFF99BBCCu+i));}
                Put(std::uintptr_t(record+0x5C),1.3f);Put(std::uintptr_t(record+0x60),.7f);}
        }
        Put(std::uintptr_t(geometry),count);Put(std::uintptr_t(geometry+8),Ptr(records.data()));
        for(unsigned slot=0;slot<2;++slot){Put(std::uintptr_t(entry+0x100+slot*4),Ptr(geometry));Put(std::uintptr_t(entry+0x114+slot*20),type);}
        if(traceActive){const auto slot=*reinterpret_cast<std::uint32_t*>(pe+0x7678A4);Put(std::uintptr_t(pe+0x7678A0),slot);
            Put(std::uintptr_t(entry+0x130+slot*4),++traceStamp);
            NativeHudKL::RecordSpriteProducer(std::uintptr_t(entry),manager,0x123456,0x81234567u,geometry,&root.basis,&root.position,17,relative?1:0);
            if(producerMismatch)Put(std::uintptr_t(entry+0x130+slot*4),++traceStamp);
        }
        NativeHudKL::SetMappedVertexFilter(filter?NativeEffectsKL::FilterMapped:nullptr);
        NativeHudKL::SpriteDrawHook(binder,nullptr,state);Check(observed.size()==std::size_t(count)*4,"actual native writer emits every authored quad and closes each original Unlock");return observed;
    };
    for(unsigned slot:{0u,1u}){Put(std::uintptr_t(pe+0x7678A4),slot);
    for(unsigned type:{5u,6u})for(unsigned count:{1u,2050u}){
        const auto baseline=run(type,count,view.reference,false,false);
        for(const auto& eye:{Eye(-.8f,.4f,.3f),Eye(.9f,-.3f,-.4f)}){
            const auto corrected=run(type,count,eye,false,true);const auto native=run(type,count,eye,false,false);bool nativeMoved=false;
            for(unsigned i=0;i<corrected.size();++i){const auto expected=World(view.reference,baseline[i]),actual=World(eye,corrected[i]);
                Check(Dot(Subtract(expected,actual),Subtract(expected,actual))<.0025f,"actual writer world corners keep authored placement under HMD yaw pitch roll");
                Check(!std::memcmp(reinterpret_cast<const unsigned char*>(&corrected[i])+12,reinterpret_cast<const unsigned char*>(&native[i])+12,12),"actual native color and UV remain bit-identical");
                const auto uncorrected=World(eye,native[i]);nativeMoved=nativeMoved||Dot(Subtract(expected,uncorrected),Subtract(expected,uncorrected))>1.f;}
            Check(nativeMoved,"unmodified original type5/6 reproduces camera-driven corner rotation");
            const auto uiNative=run(type,count,eye,true,false),uiFiltered=run(type,count,eye,true,true);
            Check(!std::memcmp(uiNative.data(),uiFiltered.data(),uiNative.size()*sizeof(Vertex)),"FC1 camera-relative UI/effects retain byte-exact original native placement and orientation");
        }
    }}
    // Native tracer offsets come from their own producer camera. Exercise
    // the actual native writer -> pre-Unlock filter with a conflicting generic
    // reference so omitting the runtime callback is a semantic regression.
    tracerEntry=std::uintptr_t(entry);tracerGeometry=std::uintptr_t(geometry);
    tracerReference=Eye(-.4f,.2f,.1f);const auto genericReference=view.reference;
    const auto tracerBaseline=run(6,1,tracerReference,false,false);
    view.reference=Eye(.8f,-.1f,-.3f);const auto tracerEye=Eye(-.9f,.3f,.4f);
    const auto wrongTracer=run(6,1,tracerEye,false,true);
    NativeTracerVisualKL::referenceReader=TracerReference;
    const auto fixedTracer=run(6,1,tracerEye,false,true);bool wrongMoved=false;
    for(unsigned i=0;i<4;++i){const auto expected=World(tracerReference,tracerBaseline[i]),actual=World(tracerEye,fixedTracer[i]);
        Check(Dot(Subtract(expected,actual),Subtract(expected,actual))<.0025f,"actual pre-Unlock filter uses the tracer producer camera instead of generic reference");
        const auto wrong=World(tracerEye,wrongTracer[i]);wrongMoved=wrongMoved||Dot(Subtract(expected,wrong),Subtract(expected,wrong))>1.f;}
    Check(wrongMoved,"wrong generic reference reproduces sideways tracer offsets in the actual native writer consumer");
    NativeTracerVisualKL::referenceReader=nullptr;view.reference=genericReference;
    for(unsigned type:{5u,6u}){
        const auto eye=Eye(.9f,.3f,.4f);const auto native=run(type,1,eye,false,false);
        NativeHudKL::SetNativeScopeProvider(ScopeOverlay);scopeOverlay=true;
        const auto scope=run(type,1,eye,false,true);
        Check(!std::memcmp(native.data(),scope.data(),native.size()*sizeof(Vertex)),"owned native scope HUD is excluded even with FC0 and a valid main camera");
        scopeOverlay=false;NativeHudKL::SetNativeScopeProvider(nullptr);
        view.state=std::uintptr_t(state)+4;
        const auto foreign=run(type,1,eye,false,true);
        Check(!std::memcmp(native.data(),foreign.data(),native.size()*sizeof(Vertex)),"foreign native renderer state cannot borrow the accepted camera");view.state=std::uintptr_t(state);
        NativeCameraKL::validView=false;const auto inactive=run(type,1,eye,false,true);
        Check(!std::memcmp(native.data(),inactive.data(),native.size()*sizeof(Vertex)),"GUI/inactive view retains original native world sprites");NativeCameraKL::validView=true;
    }
    const auto eye=Eye(-.6f,.4f,-.2f);
    const auto regular=run(6,1,eye,false,true);const auto savedCapacity=bufferControl.capacity;
    bufferControl.capacity=3u*1024u*1024u;
    const auto large=run(6,1,eye,false,true);
    Check(!std::memcmp(regular.data(),large.data(),regular.size()*sizeof(Vertex)),"a valid bounded world-sprite lease works even when native VB exceeds optional HUD mirror budget");
    bufferControl.capacity=savedCapacity;
    // A source record is borrowed only until its exact native draw returns.
    // An unexpected delayed Unlock must not touch a dangling record pointer.
    NativeHudKL::InvalidateDevice();NativeHudKL::SetMappedVertexFilter(NativeEffectsKL::FilterMapped);
    auto* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    Check(inaccessible!=nullptr,"inaccessible old native source record fixture");
    NativeHudKL::particleDrawContext={};auto& context=NativeHudKL::particleDrawContext;
    context.sourceState=std::uintptr_t(state);context.sourceEntry=std::uintptr_t(entry);context.spriteType=6;
    context.spriteRecords=std::uintptr_t(inaccessible);context.spriteCount=1;context.spriteDrawId=123;
    unsigned first=0;bufferControl.end=4*sizeof(Vertex);bufferControl.mapped=Ptr(mappedVertices.data());
    NativeHudKL::RegisterWrite(&bufferControl,mappedVertices.data(),4,24,&first,NativeHudShadow::Kind::Vertex,NativeHudKL::image+0x996BF);
    const auto savedVertices=mappedVertices;const auto before=NativeEffectsKL::Snapshot();
    NativeHudKL::particleDrawContext={};allocated=4;NativeHudKL::BufferUnlockHook(&bufferControl,nullptr);
    Check(NativeEffectsKL::Snapshot().worldSpriteRejected==before.worldSpriteRejected+1&&
        !std::memcmp(mappedVertices.data(),savedVertices.data(),4*sizeof(Vertex)),"delayed foreign Unlock cannot dereference expired source records or alter native vertices");
    VirtualFree(inaccessible,0,MEM_RELEASE);
    const auto sources=NativeHudKL::Snapshot();
    Check(sources.spriteSourceTypes[5]&&sources.spriteSourceTypes[6]&&sources.lastWorldSpriteEligible.valid&&
        sources.lastWorldSpriteEligible.anchorKnown&&sources.lastWorldSpriteEligible.reason==0&&
        sources.lastWorldSpriteEligible.entry==std::uintptr_t(entry)&&sources.lastWorldSpriteEligible.sourceState==std::uintptr_t(state)&&
        sources.lastWorldSpriteEligible.geometry==std::uintptr_t(geometry)&&sources.lastWorldSpriteEligible.drawScopeId,
        "automatic coherent eligible snapshot identifies actual native source anchor state entry geometry and exact draw scope");
    Check(sources.lastWorldSpritePreserved.valid&&sources.lastWorldSpritePreserved.reason&&sources.lastWorldSpritePreserved.type==6,
        "automatic preserved snapshot and source type histogram remain available without enabling diagnostics");
    // Root's recorder asks after the actual draw, while native SpriteDraw is
    // still active. Provenance must follow this entry/slot/stamp, never the
    // globally last HUD source or a deferred draw after the scope has ended.
    traceEntry=std::uintptr_t(entry);traceState=std::uintptr_t(state);view.frame=42;
    NativeEffectsKL::ConfigureVisualSourceDiagnostics(TraceActive);traceActive=true;
    run(6,2050,eye,false,true);
    Check(traceCaptured.valid&&traceCaptured.kind==1&&traceCaptured.type==6&&traceCaptured.entry==traceEntry&&traceCaptured.state==traceState,
        "actual native draw obtains its exact sprite source instead of last HUD snapshot");
    Check(traceCaptured.producer.valid&&traceCaptured.producer.callerRva==0x123456&&traceCaptured.producer.textureHandle==0x81234567u&&
        traceCaptured.producer.stamp==traceCaptured.stamp&&traceCaptured.producer.storedGeometry==std::uintptr_t(geometry),
        "producer registry matches exact CPU/GPU slot stamp material and geometry before reporting caller or asset handle");
    Check(traceCaptured.rawBeforeKnown&&traceCaptured.rawAfterKnown&&traceCaptured.recordsKnown&&traceCaptured.recordStride==116&&
        traceCaptured.firstRecord==2048&&traceCaptured.recordCount==2&&traceCaptured.sourceCount==2050&&traceCaptured.viewKnown&&traceCaptured.frame==42,
        "captured actual second native batch retains raw before/after quad source prefix and camera reference");
    NativeEffectsKL::VisualSourceDiagnostic missing;
    Check(!NativeEffectsKL::SnapshotVisualSource(traceEntry,traceState,missing),"deferred draw cannot borrow the completed sprite's stale diagnostic scope");
    producerMismatch=true;run(6,1,eye,false,true);producerMismatch=false;
    Check(traceCaptured.valid&&!traceCaptured.producer.valid,"changed native stamp reports source metadata honestly without borrowing an older emitter");
    traceActive=false;
    Check(!NativeEffectsKL::SnapshotVisualSource(traceEntry,traceState,missing),"inactive trace avoids source lookup and heavy source capture");
    NativeEffectsKL::ConfigureVisualSourceDiagnostics(nullptr);
    std::printf("Actual native world sprite5/6: %u checks PASS; CPU only.\n",checks);VirtualFree(pe,0,MEM_RELEASE);
}
