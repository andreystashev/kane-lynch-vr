// Original pinned 99551/996BF writer -> real pre-Unlock canonicalization ->
// real marker c0 consumer. Private PE/CPU only; no game, window, D3D or HMD.
#include "native_hud_kl.cpp"
#include "native_camera_kl_math.hpp"
#include "native_effects_image_kl.hpp"
#include <fstream>
#include <iterator>
#include <cstdlib>

namespace NativeCameraKL {std::uintptr_t CinematicCamera(){return 0;}bool CinematicPanelActive(){return false;}}
namespace {
using namespace NativeCameraMath;using Vertex=NativeHudMath::Vertex;
unsigned checks=0,allocated=0;std::uintptr_t fixtureImage=0;
unsigned nativeIndexedCalls=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
std::uint32_t Ptr(const void* p){return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));}
LONG WINAPI Fault(EXCEPTION_POINTERS* info){const auto* c=info->ContextRecord;
    std::fprintf(stderr,"marker CPU fault %08lx EIP=%08lx RVA=%08lx\n",info->ExceptionRecord->ExceptionCode,c->Eip,static_cast<unsigned long>(c->Eip-fixtureImage));ExitProcess(1);}
std::array<Vertex,4> mapped{},observed{};
struct Device {void** table;};struct Resource {void** table;};
void* deviceTable[16]{},*resourceTable[16]{};Device device{deviceTable};Resource resource{resourceTable};
NativeHudKL::NativeBufferControl bufferControl;NativeHudKL::ReticleFrame frame;
NativeHudKL::MarkerPacket lastPacket;std::uint64_t lastDrawId=0;
bool consumerAccepted=false,consumerReceipt=false;unsigned consumerStage=0;float centreError=0;
ULONG STDMETHODCALLTYPE Release(void*){return 1;}
HRESULT STDMETHODCALLTYPE GetDevice(void*,IDirect3DDevice9** output){*output=reinterpret_cast<IDirect3DDevice9*>(&device);return S_OK;}
HRESULT STDMETHODCALLTYPE GetDesc(void*,D3DVERTEXBUFFER_DESC* output){*output={};output->Type=D3DRTYPE_VERTEXBUFFER;output->Size=bufferControl.capacity;return S_OK;}
void* __thiscall Lock(void*,UINT count,UINT stride,UINT* first){
    Check(count==4&&stride==24,"native image writer requests exact single quad");allocated=count;*first=0;bufferControl.end=count*stride;bufferControl.mapped=Ptr(mapped.data());return mapped.data();}
void __thiscall Unlock(void*){Check(allocated==4,"native writer closes exact quad");observed=mapped;}
unsigned __fastcall NoEntryContext(void*,void*){return 0;}
unsigned __fastcall BufferPosition(void*,void*,unsigned,unsigned){return 0;}
void __fastcall BindStream(void*,void*,unsigned,unsigned){}
HRESULT __thiscall NativeIndexed(void*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT){++nativeIndexedCalls;return D3D_OK;}
void __cdecl Draw(unsigned,unsigned,unsigned){
    if(NativeHudKL::nativeMarkerDrawing){lastPacket=NativeHudKL::nativeMarkerDrawPacket;lastDrawId=NativeHudKL::markerDrawId;}
    const float native[]={1,0,0,0,0,1,0,0,0,0,1,-5,0,0,1,0};float rows[16]{};
    std::vector<Vertex> vertices(observed.begin(),observed.end());consumerStage=0;
    consumerReceipt=NativeHudKL::markerDrawId&&NativeHudKL::markerCanonicalizedDrawId==NativeHudKL::markerDrawId;
    consumerAccepted=NativeHudKL::TranslateNativeMarker(NativeHudKL::nativeMarkerDrawPacket,frame,native,vertices,rows,consumerStage);
    if(!consumerAccepted)return;
    float x=0,y=0;for(const auto& v:observed){float clip[4]{};for(unsigned r=0;r<4;++r)clip[r]=v.x*rows[r*4]+v.y*rows[r*4+1]+v.z*rows[r*4+2]+rows[r*4+3];x+=clip[0]/clip[3];y+=clip[1]/clip[3];}
    const auto delta=Subtract(frame.target,frame.actualEye.position);const float depth=Dot(delta,frame.actualEye.basis.forward);
    const float expectedX=Dot(delta,frame.actualEye.basis.right)*frame.projection[0]/depth+frame.projection[8];
    const float expectedY=Dot(delta,frame.actualEye.basis.up)*frame.projection[5]/depth+frame.projection[9];
    centreError=std::hypot(x*.25f-expectedX,y*.25f-expectedY);
}
void Jump(unsigned char* at,const void* target){at[0]=0xE9;Put(std::uintptr_t(at+1),std::int32_t(std::uintptr_t(target)-(std::uintptr_t(at)+5)));}
CameraPose Eye(float yaw){const Vec3 f{std::sin(yaw),0,std::cos(yaw)},r{std::cos(yaw),0,-std::sin(yaw)};return {{f,{0,1,0},r},{80,140,-30}};}
}
int main(int argc,char** argv){
    if (argc < 2) {
        std::puts("Pass a supported game executable for the native fixture.");
        return 2;
    }
    const char* path = argv[1];

    const std::wstring wide(path,path+std::strlen(path));char digest[65]{};
    Check(HashFile(wide.c_str(),digest)&&IsSupportedExecutableHash(digest),"native precision fixture uses SHA256-pinned supported EXE");
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});Check(!bytes.empty(),"read pinned EXE without starting game");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    const auto size=nt->OptionalHeader.SizeOfImage;auto* pe=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(pe&&Ptr(pe)<0x80000000u,"private low-address native CPU image");fixtureImage=std::uintptr_t(pe);AddVectoredExceptionHandler(1,Fault);
    std::memcpy(pe,bytes.data(),nt->OptionalHeader.SizeOfHeaders);const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];Check(s.VirtualAddress+s.SizeOfRawData<=size&&s.PointerToRawData+s.SizeOfRawData<=bytes.size(),"bounded native PE section");std::memcpy(pe+s.VirtualAddress,bytes.data()+s.PointerToRawData,s.SizeOfRawData);}
    const auto originalRead=[&](std::uintptr_t at,void* out,std::size_t count){if(at<0x400000u||at-0x400000u+count>size)return false;std::memcpy(out,pe+at-0x400000u,count);return true;};
    Check(NativeEffectsKLImage::Verify(0x400000u,originalRead),"exact native type5/6 writer and binder opcodes");
    const auto delta=Ptr(pe)-0x400000u;
    for(unsigned off:{0x990AFu,0x990E1u,0x990FAu,0x9912Eu,0x9914Fu,0x99170u,0x99180u,0x99195u,0x991ACu,0x991ECu,
        0x99241u,0x99257u,0x992C2u,0x992C9u,0x993ECu,0x99405u,0x994DAu,0x995AEu,0x995CAu,0x995E2u,0x995EFu,0x99673u,0x71DA1u,0x71DD5u}){
        std::uint32_t value=0;std::memcpy(&value,pe+off,4);Check(value>=0x400000u&&value<0x400000u+size,"explicit executed absolute operand");Put(std::uintptr_t(pe+off),value+delta);}
    for(unsigned i=0;i<7;++i)*reinterpret_cast<std::uint32_t*>(pe+0x9A1F4+i*4)+=delta;
    Check(pe[0x990DD]==0x75&&pe[0x990DE]==0x2C,"exact native material-lookup join");Jump(pe+0x990DD,pe+0x9923F);pe[0x990E2]=0x90;
    Jump(pe+0x8E150,reinterpret_cast<const void*>(&NativeHudKL::VertexLockHook));Jump(pe+0x8E350,reinterpret_cast<const void*>(&NativeHudKL::BufferUnlockHook));
    Jump(pe+0x8E290,reinterpret_cast<const void*>(&BufferPosition));Jump(pe+0x8DD60,reinterpret_cast<const void*>(&BindStream));Jump(pe+0x984F0,reinterpret_cast<const void*>(&Draw));
    pe[0x4BABA4]=0xD9;pe[0x4BABA5]=0xFA;pe[0x4BABA6]=0xC3;
    Put(std::uintptr_t(pe+0x540EAC+0x20),Ptr(reinterpret_cast<const void*>(&NoEntryContext)));
    NativeHudKL::image=std::uintptr_t(pe);NativeHudKL::originalVertexLock=Lock;NativeHudKL::originalBufferUnlock=Unlock;
    NativeHudKL::originalSpriteDraw=reinterpret_cast<NativeHudKL::SpriteDraw>(pe+0x990A0);
    deviceTable[2]=reinterpret_cast<void*>(&Release);resourceTable[3]=reinterpret_cast<void*>(&GetDevice);resourceTable[13]=reinterpret_cast<void*>(&GetDesc);
    bufferControl.resource=Ptr(&resource);bufferControl.capacity=sizeof(mapped);
    alignas(16) unsigned char state[0x500]{},entry[0x150]{},binder[0x20]{},material[0x40]{},materialOuter[0x10]{},manager[0x30]{},geometry[16]{},nativeDevice[0x30]{};
    alignas(16) unsigned char sourceCamera[0x150]{},ui[0x60]{},uiVtable[0x248]{},record[0x64]{};
    Put(std::uintptr_t(pe+0x786670),Ptr(manager));Put(std::uintptr_t(manager+0x1C),Ptr(&bufferControl));Put(std::uintptr_t(pe+0x786658),Ptr(nativeDevice));Put(std::uintptr_t(state+0x10),Ptr(nativeDevice));
    Put(std::uintptr_t(binder),Ptr(pe+0x53FB88));Put(std::uintptr_t(binder+4),Ptr(materialOuter));Put(std::uintptr_t(materialOuter+4),Ptr(material));Put(std::uintptr_t(binder+0xC),Ptr(entry));Put(std::uintptr_t(entry),Ptr(pe+0x540EAC));
    Put(std::uintptr_t(sourceCamera+0x14C),Ptr(ui));Put(std::uintptr_t(ui),Ptr(uiVtable));Put(std::uintptr_t(uiVtable+0x244),Ptr(pe+0xF5BD0));
    Put(std::uintptr_t(ui+0x38),1.f);Put(std::uintptr_t(ui+0x3C),16.f/9.f);Put(std::uintptr_t(ui+0x54),.5f);
    Put(std::uintptr_t(record+0x1C),std::uint32_t(24));record[0x4D]=1;
    using Bind=void(__thiscall*)(void*,void*,void*);const auto bind=reinterpret_cast<Bind>(pe+0x71DA0);
    std::uint32_t stamp=0;std::uint64_t pair=1;
    const auto run=[&](unsigned type,float magnitude,float depth,float yaw,bool canonical,bool owned=true){
        NativeHudKL::InvalidateDevice();consumerAccepted=consumerReceipt=false;centreError=0;consumerStage=0;entry[0xFC]=1;
        const auto eye=Eye(yaw);CameraPose inverse;Check(NativeCameraKLMath::ViewFromWorld(eye,inverse),"native inverse current eye");Put(std::uintptr_t(state+0xBC),inverse);
        const CameraPose root{{{0,0,1},{0,1,0},{1,0,0}},{magnitude*1.0012345f,-magnitude*.7734567f,depth}};
        Put(std::uintptr_t(entry+0x10),root);Put(std::uintptr_t(entry+0x40),root);bind(entry,state,nullptr);
        std::array<float,29> source{};
        if(type==5){source[3]=0;source[4]=1;source[5]=0;source[6]=1;source[7]=source[8]=100;Put(std::uintptr_t(source.data()+9),std::uint32_t(0xFF70E5FF));}
        else{const Vec3 corners[]={{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}};
            const float uv[]={0,0,1,0,1,1,0,1};std::memcpy(source.data()+3,uv,sizeof(uv));for(unsigned i=0;i<4;++i){std::memcpy(source.data()+11+i*3,&corners[i],12);Put(std::uintptr_t(source.data()+25+i),std::uint32_t(0xFF70E5FFu+i));}source[23]=source[24]=100;}
        Put(std::uintptr_t(geometry),std::uint32_t(1));Put(std::uintptr_t(geometry+8),Ptr(source.data()));
        const unsigned slot=stamp&1;Put(std::uintptr_t(pe+0x7678A0),slot);Put(std::uintptr_t(pe+0x7678A4),slot);Put(std::uintptr_t(entry+0x130+slot*4),++stamp);
        Put(std::uintptr_t(entry+0x100+slot*4),Ptr(geometry));Put(std::uintptr_t(entry+0x114+slot*20),type);
        frame={};frame.player=0x12340000;frame.pairId=++pair;frame.tick=GetTickCount();frame.width=3840;frame.height=2160;frame.actualEye=frame.centre=eye;frame.target=Add(eye.position,Scale(eye.basis.forward,500));
        const float fov[]={1.1f,.9f,1.f,1.f};Check(ApplyEyeProjection(frame.projection,fov,1,10000,false),"actual SDK asymmetric projection");std::memcpy(frame.referenceProjection,frame.projection,sizeof(frame.projection));
        if(owned){NativeHudKL::BeginNativeMarkers(frame.player,manager,sourceCamera);auto& scope=NativeHudKL::nativeMarkerScope;scope.record=std::uintptr_t(record);scope.target=frame.target;scope.valid=true;
            NativeHudKL::CaptureMarker(std::uintptr_t(entry),frame.player,geometry,&root.position,0x26,0x253CF5);NativeHudKL::EndNativeMarkers();
            if(!canonical)for(auto& packet:NativeHudKL::markerPackets)if(packet.entry==std::uintptr_t(entry))packet.canonical.valid=false;
        }
        NativeHudKL::SpriteDrawHook(binder,nullptr,state);return observed;
    };
    unsigned oldRejected=0;float oldMaximumError=0;
    for(unsigned type:{5u,6u})for(float magnitude:{0.f,10000.f,1000000.f,100000000.f,10000000000.f})for(float depth:{1322.2103f,-1322.2103f})for(float yaw:{-.8f,.9f}){
        const auto original=run(type,magnitude,depth,yaw,false);if(!consumerAccepted){Check(consumerStage==44,"old absolute native-coordinate failure reaches marker math stage44");++oldRejected;}else oldMaximumError=std::max(oldMaximumError,centreError);
        const auto corrected=run(type,magnitude,depth,yaw,true);
        Check(consumerAccepted&&consumerReceipt,"actual native writer pre-Unlock lease and actual marker consumer accept canonical image coordinates");
        Check(centreError<.00001f,"actual native type5/6 shader projection centre stays on fixed WORLD goal despite huge native UI centre");
        for(unsigned i=0;i<4;++i){Check(std::fabs(corrected[i].x)<=1&&std::fabs(corrected[i].y)<=1,"canonical XY comes from modest source corners, never reconstructed rounded native XY");
            Check(!std::memcmp(reinterpret_cast<const unsigned char*>(&original[i])+8,reinterpret_cast<const unsigned char*>(&corrected[i])+8,16),"native Z fog coordinate UV and colour remain bit-exact");}
        const auto foreign=run(type,magnitude,depth,yaw,true,false);Check(!std::memcmp(original.data(),foreign.data(),sizeof(original)),"foreign or unowned sprite stays byte-exact native");
    }
    Check(oldRejected>0,"actual unmodified native writer loses corner extents and reproduces front-visible stage44 failure");
    // The native write capability ends with this exact scope/entry/slot/
    // stamp/producer. No delayed or replaced source can rewrite VB bytes.
    auto protectedVertices=run(6,1e10f,1322.2103f,.3f,true);
    NativeHudKL::nativeMarkerDrawing=true;NativeHudKL::nativeMarkerDrawPacket=lastPacket;NativeHudKL::markerDrawId=lastDrawId;
    NativeHudKL::PendingWrite lease;lease.markerDraw=lastDrawId;lease.marker=lastPacket;lease.mapped=std::uintptr_t(protectedVertices.data());
    lease.allocation.bytes=sizeof(protectedVertices);lease.allocation.provenance=0x996BF;lease.allocation.identity.kind=NativeHudShadow::Kind::Vertex;
    const auto unmodified=protectedVertices;const auto stampAt=std::uintptr_t(entry+0x130+lastPacket.slot*4);const auto savedStamp=*reinterpret_cast<std::uint32_t*>(stampAt);
    Put(stampAt,savedStamp+1);NativeHudKL::markerCanonicalizedDrawId=lastDrawId;
    Check(!NativeHudKL::CanonicalizeMarkerWrite(lease)&&!NativeHudKL::markerCanonicalizedDrawId&&!std::memcmp(unmodified.data(),protectedVertices.data(),sizeof(unmodified)),"changed native stamp rejects write before touching bytes and revokes its old receipt");Put(stampAt,savedStamp);
    auto foreignLease=lease;++foreignLease.marker.producer;NativeHudKL::markerCanonicalizedDrawId=lastDrawId;
    Check(!NativeHudKL::CanonicalizeMarkerWrite(foreignLease)&&!NativeHudKL::markerCanonicalizedDrawId&&!std::memcmp(unmodified.data(),protectedVertices.data(),sizeof(unmodified)),"foreign same-entry producer cannot borrow marker write capability");
    protectedVertices[0].z=std::numeric_limits<float>::quiet_NaN();const auto badZ=protectedVertices;
    Check(!NativeHudKL::CanonicalizeMarkerWrite(lease)&&!std::memcmp(badZ.data(),protectedVertices.data(),sizeof(badZ)),"nonfinite native Z is rejected instead of inventing a fog coordinate");
    protectedVertices=unmodified;protectedVertices[0].x=std::numeric_limits<float>::quiet_NaN();
    Check(NativeHudKL::CanonicalizeMarkerWrite(lease)&&NativeHudKL::markerCanonicalizedDrawId==lastDrawId&&std::isfinite(protectedVertices[0].x),"owned nonfinite native XY can be replaced from finite canonical source while preserving native Z");
    auto* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);Check(inaccessible!=nullptr,"private expired write arena");
    lease.mapped=std::uintptr_t(inaccessible);NativeHudKL::markerDrawId=0;NativeHudKL::nativeMarkerDrawing=false;
    Check(!NativeHudKL::CanonicalizeMarkerWrite(lease),"delayed write rejects expired scope before dereferencing inaccessible bytes");VirtualFree(inaccessible,0,MEM_RELEASE);
    NativeHudKL::originalIndexed=NativeIndexed;NativeHudKL::nativeMarkerDrawPacket=lastPacket;frame.tick=GetTickCount();
    bool fallback=false;const auto rejectedBefore=NativeHudKL::Snapshot().nativeMarkerRejected;
    Check(NativeHudKL::DrawReticle(nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2,&frame,&fallback)==D3D_OK&&fallback&&
        NativeHudKL::Snapshot().nativeMarkerRejectStage==50&&NativeHudKL::Snapshot().nativeMarkerRejected==rejectedBefore+1&&!nativeIndexedCalls,
        "actual marker consumer suppresses an expired canonical receipt before any D3D access or native fallback");
    NativeHudKL::markerDrawId=NativeHudKL::markerCanonicalizedDrawId=lastDrawId;Put(stampAt,savedStamp+1);fallback=false;
    Check(NativeHudKL::DrawReticle(nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2,&frame,&fallback)==D3D_OK&&fallback&&
        NativeHudKL::Snapshot().nativeMarkerRejectStage==50&&!nativeIndexedCalls,
        "actual consumer rechecks native source stamp after canonicalization and never exposes canonical XY with original native c0");Put(stampAt,savedStamp);
    NativeHudKL::nativeMarkerDrawing=true;
    Check(NativeHudKL::IndexedHook(nullptr,nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2)==D3D_OK&&
        NativeHudKL::Snapshot().nativeMarkerRejectStage==51&&!nativeIndexedCalls,"unexpected indexed caller cannot pass canonical source vertices to the native fallback");
    NativeHudKL::nativeMarkerDrawing=false;NativeHudKL::markerDrawId=NativeHudKL::markerCanonicalizedDrawId=0;
    Check(NativeHudKL::IndexedHook(nullptr,nullptr,D3DPT_TRIANGLELIST,0,0,4,0,2)==D3D_OK&&nativeIndexedCalls==1,"unowned indexed caller retains its normal native draw");
    const auto counters=NativeHudKL::Snapshot();Check(counters.markerCanonicalized&&counters.markerCanonicalMaxMagnitude>1e9f,"actual canonicalization counters expose original large centre magnitude");
    std::printf("Native marker original writer/canonical consumer: %u checks PASS; old rejects=%u, old maximum NDC centre error=%g.\n",checks,oldRejected,oldMaximumError);
    NativeHudKL::InvalidateDevice();VirtualFree(pe,0,MEM_RELEASE);return 0;
}
