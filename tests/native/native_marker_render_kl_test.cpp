// Exact production Position -> emitted native sprite packet -> deferred
// IndexedHook translation, with real hidden HAL pixels. Native functions are
// private fixture callbacks; the supported EXE is only read for opcode proof.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace Fixture {inline DWORD tick=1000;inline DWORD Clock(){return tick;}}
#define GetTickCount Fixture::Clock
#include "native_hud_kl.cpp"
#undef GetTickCount
#include "native_hud_test_fixture.hpp"
#include <cstdio>
#include <cstdlib>
namespace NativeCameraKL {std::uintptr_t CinematicCamera(){return 0;}bool CinematicPanelActive(){return true;}}
namespace Fixture {
unsigned checks=0;IDirect3DDevice9* device=nullptr;std::uintptr_t entry=0;
unsigned nativeDrawCalls=0;bool requireOriginalState=false,failScissor=false;
IDirect3DTexture9* expectedNativeTexture=nullptr;bool verifyNativeResources=false;
bool verifyRoundGeometry=false;
constexpr DWORD NativeColour=0xffe0c060;
const float nativeRows[]={1,0,0,0,0,1,0,0,0,0,1,-5,0,0,1,0};
using SetState=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD);
SetState originalSetState=nullptr;
struct DeviceProxy {void** table=nullptr;IDirect3DDevice9* real=nullptr;};
NativeHudKL::ReticleFrame frame;
using Emit=std::uintptr_t(__thiscall*)(void*,std::uint32_t,const void*,const void*,const void*,std::uint32_t,std::uint32_t,std::uint32_t);
using Indexed=HRESULT(__thiscall*)(void*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
Indexed indexed=nullptr;
struct Backend {std::uint32_t vt=0,device=0;} backend;
void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL: %s\n",text);std::exit(1);}}
void Hr(HRESULT result,const char* text){if(FAILED(result))std::printf("HRESULT %08lx\n",static_cast<unsigned long>(result));Check(SUCCEEDED(result),text);}
template<class T>void Put(std::uintptr_t at,const T& value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
template<class T>void Release(T*& value){if(value){value->Release();value=nullptr;}}
bool Provider(NativeHudKL::ReticleFrame& out){out=frame;return true;}
void* __thiscall Position(void* object,void* output){auto* p=static_cast<unsigned char*>(object);NativeCameraMath::Vec3 world;
    std::memcpy(&world,p+4,sizeof(world));if(p[0x4D])world.y+=30.f;else if(p[0x4F]){float bob=0;std::memcpy(&bob,p+0x40,4);world.y+=bob;}std::memcpy(output,&world,sizeof(world));return output;}
std::uintptr_t __thiscall EmitNative(void*,std::uint32_t,const void*,const void*,const void*,std::uint32_t,std::uint32_t,std::uint32_t){return entry;}
HRESULT __thiscall DrawNative(void*,D3DPRIMITIVETYPE primitive,INT base,UINT first,UINT count,UINT start,UINT primitives){
    ++nativeDrawCalls;
    if(verifyRoundGeometry){float rows[16]{};Hr(device->GetVertexShaderConstantF(0,rows,4),"read actual objective billboard projection");
        const float width=std::fabs(rows[0]*.6f/rows[15]/frame.projection[0]);
        const float height=std::fabs(rows[5]*.6f/rows[15]/frame.projection[5]);
        Check(std::fabs(width-height)<1e-5f,"actual native circular objective has equal angular/world diameters in the SDK eye");}
    if(verifyNativeResources){IDirect3DBaseTexture9* bound=nullptr;
        Hr(device->GetTexture(0,&bound),"read original native marker atlas at actual draw");
        Check(bound==expectedNativeTexture,"original native marker atlas is retained instead of a custom texture");Release(bound);}
    if(requireOriginalState){float rows[16]{};DWORD scissor=FALSE;
        Hr(device->GetVertexShaderConstantF(0,rows,4),"query constants before late native fallback");
        Hr(device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor),"query scissor before late native fallback");
        Check(!std::memcmp(rows,nativeRows,sizeof(rows))&&scissor==TRUE,"native fallback begins only after bit-exact projection and scissor restoration");}
    return device->DrawIndexedPrimitive(primitive,base,first,count,start,primitives);}
HRESULT STDMETHODCALLTYPE FaultState(IDirect3DDevice9* object,D3DRENDERSTATETYPE state,DWORD value){
    if(failScissor&&state==D3DRS_SCISSORTESTENABLE&&value==FALSE){failScissor=false;return E_FAIL;}
    return originalSetState(reinterpret_cast<DeviceProxy*>(object)->real,state,value);
}
std::uintptr_t __thiscall SpriteNative(void*,void*){Hr(device->BeginScene(),"begin private native sprite scene");
    // This HAL fixture supplies already small CPU-captured quad coordinates
    // instead of executing the native writer. The separate writer CPU test
    // proves pre-Unlock canonicalization and its ownership receipt.
    if(NativeHudKL::nativeMarkerDrawing&&NativeHudKL::nativeMarkerDrawPacket.canonical.valid)
        NativeHudKL::markerCanonicalizedDrawId=NativeHudKL::markerDrawId;
    const auto result=indexed(&backend,D3DPT_TRIANGLELIST,0,0,4,0,2);Hr(device->EndScene(),"end private native sprite scene");return static_cast<std::uintptr_t>(result);}
// Private executable forwarding thunk preserves the actual native return-PC
// provenance checked by production hooks. It never executes game image code.
void* Thunk(std::uintptr_t image,unsigned returnRva,void* function,unsigned arguments) {
    std::vector<unsigned char> code{0x55,0x8B,0xEC};
    for(unsigned i=arguments;i;--i){code.push_back(0xFF);code.push_back(0x75);code.push_back(static_cast<unsigned char>(4+i*4));}
    code.push_back(0xB8);const auto target=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(function));
    for(unsigned i=0;i<4;++i)code.push_back(static_cast<unsigned char>(target>>(8*i)));
    code.push_back(0xFF);code.push_back(0xD0);const auto before=code.size();code.push_back(0xC9);code.push_back(0xC2);
    code.push_back(static_cast<unsigned char>(arguments*4));code.push_back(0);
    const auto address=image+returnRva-before;std::memcpy(reinterpret_cast<void*>(address),code.data(),code.size());
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),code.size());return reinterpret_cast<void*>(address);
}
struct Coverage {unsigned pixels=0;double centre=0;unsigned changedColour=0;};
Coverage Pixels(IDirect3DSurface9* backbuffer){IDirect3DSurface9* copy=nullptr;
    Hr(device->CreateOffscreenPlainSurface(320,240,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr),"create HAL pixel readback");
    Hr(device->GetRenderTargetData(backbuffer,copy),"read actual native-style marker GPU output");D3DLOCKED_RECT lock{};
    Hr(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY),"lock HAL marker pixels");Coverage out;
    for(unsigned y=0;y<240;++y){const auto* row=reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch);
        for(unsigned x=0;x<320;++x)if(row[x]&0xffffff){++out.pixels;out.centre+=x;if((row[x]&0xffffff)!=(NativeColour&0xffffff))++out.changedColour;}}
    if(out.pixels)out.centre/=out.pixels;copy->UnlockRect();Release(copy);return out;}
}
int main(int argc,char** argv){
    using namespace Fixture;using namespace NativeHudKL;
    static_assert(sizeof(void*)==4,"native production routing fixture is x86");
    if(argc>1){NativeHudFixture::Image actual;Check(actual.Load(argv[1]),"read pinned PE without starting game");
        Check(NativeTeamMarkerKL::VerifyImage(NativeHudFixture::Base,actual.Reader()),"actual native position/lift/index/texture proof qualifies fixture");}
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"KL1NativeMarkerFixture";
    RegisterClassW(&cls);HWND window=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,cls.hInstance,nullptr);
    Check(window!=nullptr,"create hidden private fixture window");auto* api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"create D3D9");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    Hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device),"create real HAL marker device");
    IDirect3DSurface9* backbuffer=nullptr;Hr(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer),"get HAL backbuffer");
    const DWORD vsCode[]={0xfffe0101,0x1f,0x80000000,0x900f0000,0x1f,0x8000000a,0x900f0001,0x1f,0x80000005,0x900f0002,
        9,0xc0010000,0x90e40000,0xa0e40000,9,0xc0020000,0x90e40000,0xa0e40001,
        9,0xc0040000,0x90e40000,0xa0e40002,9,0xc0080000,0x90e40000,0xa0e40003,
        1,0xd00f0000,0x90e40001,1,0xe0030000,0x90e40002,0xffff};
    const DWORD psCode[]={0xffff0101,1,0x800f0000,0x90e40000,0xffff};
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    Hr(device->CreateVertexShader(vsCode,&vs),"compile native-style UI row projection shader");Hr(device->CreatePixelShader(psCode,&ps),"compile native colour preservation shader");
    Hr(device->SetVertexShader(vs),"bind native-style shader");Hr(device->SetPixelShader(ps),"bind colour shader");
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},
        {0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration=nullptr;Hr(device->CreateVertexDeclaration(elements,&declaration),"create exact native24byte declaration");
    Hr(device->SetVertexDeclaration(declaration),"bind native declaration");
    const NativeHudMath::Vertex vertices[]={{.3f,-.3f,6,NativeColour,0,1},{.9f,-.3f,6,NativeColour,1,1},{.9f,.3f,6,NativeColour,1,0},{.3f,.3f,6,NativeColour,0,0}};
    const std::uint16_t indices[]={0,1,2,0,2,3};IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* memory=nullptr;
    Hr(device->CreateVertexBuffer(sizeof(vertices),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr),"create native WRITEONLY vertices");
    Hr(vb->Lock(0,sizeof(vertices),&memory,0),"native vertex write");std::memcpy(memory,vertices,sizeof(vertices));vb->Unlock();
    Hr(device->CreateIndexBuffer(sizeof(indices),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr),"create native WRITEONLY indices");
    Hr(ib->Lock(0,sizeof(indices),&memory,0),"native index write");std::memcpy(memory,indices,sizeof(indices));ib->Unlock();
    Hr(device->SetStreamSource(0,vb,0,sizeof(vertices[0])),"bind native vertices");Hr(device->SetIndices(ib),"bind native indices");
    Hr(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"native UI depth-write off");Hr(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS),"native UI depth ALWAYS");
    Hr(device->SetRenderState(D3DRS_ZENABLE,FALSE),"disable private depth");Hr(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"native UI no cull");
    Hr(device->SetVertexShaderConstantF(0,nativeRows,4),"set original native UI projection");
    Hr(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&expectedNativeTexture,nullptr),"create original native atlas identity sentinel");
    Hr(device->SetTexture(0,expectedNativeTexture),"bind original native atlas identity sentinel");verifyNativeResources=true;
    auto* privateImage=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));Check(privateImage!=nullptr,"allocate private provenance/state image");
    image=reinterpret_cast<std::uintptr_t>(privateImage);entry=image+0x10000;backend.device=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(device));
    Put(image+0x7678A0,std::uint32_t(0));Put(image+0x7678A4,std::uint32_t(0));Put(entry,std::uint32_t(image+0x540EAC));
    Put(entry+0x100,std::uint32_t(image+0x20000));Put(entry+0x130,std::uint32_t(71));const std::uint32_t material[]={1,32,32,5,0};Put(entry+0x108,material);
    originalMarkerPosition=Position;originalSpriteEmit=EmitNative;originalSpriteDraw=SpriteNative;originalIndexed=DrawNative;
    const auto emitter=reinterpret_cast<Emit>(Thunk(image,0x253CF5,reinterpret_cast<void*>(&SpriteEmitHook),7));
    const auto nativeCamera=image+0xF0000,uiCamera=image+0xF1000;
    Put(nativeCamera+0x14C,std::uint32_t(uiCamera));Put(uiCamera,std::uint32_t(image+0x545A3C));Put(image+0x545A3C+0x244,std::uint32_t(image+0xF5BD0));
    Put(uiCamera+0x38,1.f);Put(uiCamera+0x3C,1.f);Put(uiCamera+0x54,.5f);
    std::array<float,29> authored{};authored[7]=authored[8]=.6f;
    std::array<std::uint32_t,3> authoredHeader{1,0,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(authored.data()))};
    NativeCameraMath::Vec3 spritePosition{0,0,6};
    const auto emitMarker=[&](){return emitter(reinterpret_cast<void*>(0x654321),1,authoredHeader.data(),nullptr,&spritePosition,10,1,0);};
    indexed=reinterpret_cast<Indexed>(Thunk(image,0x985C5,reinterpret_cast<void*>(&IndexedHook),6));
    const NativeHudShadow::Allocation va{{reinterpret_cast<std::uintptr_t>(vb),reinterpret_cast<std::uintptr_t>(device),1,sizeof(vertices),NativeHudShadow::Kind::Vertex},0,sizeof(vertices),true,0x996BF,tick};
    const NativeHudShadow::Allocation ia{{reinterpret_cast<std::uintptr_t>(ib),reinterpret_cast<std::uintptr_t>(device),2,sizeof(indices),NativeHudShadow::Kind::Index16},0,sizeof(indices),true,0x9851C,tick};
    Check(shadow.Commit(va,shadow.Begin(va),vertices,sizeof(vertices))&&shadow.Commit(ia,shadow.Begin(ia),indices,sizeof(indices)),"capture exact native writer provenance without reading WRITEONLY resources");
    frame.player=0x123400;frame.pairId=7;frame.tick=tick;frame.width=320;frame.height=240;frame.actualEye.basis={};
    const float fov[]={1,1,1,1};Check(NativeCameraMath::ApplyEyeProjection(frame.projection,fov,1,10000,false),"actual eye projection valid");
    std::memcpy(frame.referenceProjection,frame.projection,sizeof(frame.projection));SetNativeMarkerProvider(Provider);
    std::array<unsigned char,0x64> record{};NativeCameraMath::Vec3 nativePosition{250,-30,0};std::memcpy(record.data()+4,&nativePosition,sizeof(nativePosition));record[0x4D]=1;
    const std::uint32_t defendType=21;std::memcpy(record.data()+0x1C,&defendType,sizeof(defendType));
    NativeCameraMath::Vec3 position;BeginNativeMarkers(frame.player,reinterpret_cast<void*>(0x654321),reinterpret_cast<void*>(nativeCamera));
    Check(MarkerPositionHook(record.data(),nullptr,&position)==&position&&position.y==0,"production observer captures native world position after fixed lift");
    Check(emitMarker()==entry,"production emit preserves native returned sprite packet");EndNativeMarkers();
    Check(Snapshot().nativeMarkerCaptured==1&&Snapshot().nativeMarkerEmitted==1,"real producer provenance captures one native sprite with exact world source");
    Check(Snapshot().nativeDestinationCaptured==1,"actual native type21 fixed destination enters the original sprite route");
    Check(Snapshot().nativeMarkerCalibrated==1&&!Snapshot().nativeMarkerCalibrationRejected,"producer native camera and image payload calibrate size before deferred VR view");
    std::uint32_t binder[4]={static_cast<std::uint32_t>(image+0x53FB88),0,0,static_cast<std::uint32_t>(entry)};
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear left native marker image");frame.actualEye.position={0,0,3.2f};SpriteDrawHook(binder,nullptr,nullptr);const auto left=Pixels(backbuffer);
    float restored[16]{};Hr(device->GetVertexShaderConstantF(0,restored,4),"read native projection after translated sprite");Check(!std::memcmp(nativeRows,restored,sizeof(nativeRows)),"native sprite projection restored bit exactly");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear right native marker image");frame.actualEye.position={0,0,-3.2f};SpriteDrawHook(binder,nullptr,nullptr);const auto right=Pixels(backbuffer);
    std::printf("HAL native marker pixels %u / %u; disparity %.3f px.\n",left.pixels,right.pixels,left.centre-right.centre);
    const auto diagnostic=Snapshot();std::printf("Native route emits%llu captured%llu routed%llu drawn%llu projected%llu rejected%llu; readStage%lu shadowMiss%llu z%lu/%lu geometryReject%llu markerStage%u.\n",
        static_cast<unsigned long long>(diagnostic.nativeMarkerEmitted),static_cast<unsigned long long>(diagnostic.nativeMarkerCaptured),
        static_cast<unsigned long long>(diagnostic.nativeMarkerRouted),static_cast<unsigned long long>(diagnostic.nativeMarkerDrawn),
        static_cast<unsigned long long>(diagnostic.nativeMarkerReprojected),static_cast<unsigned long long>(diagnostic.nativeMarkerRejected),
        static_cast<unsigned long>(diagnostic.lastReadStage),static_cast<unsigned long long>(diagnostic.shadowMisses),static_cast<unsigned long>(diagnostic.lastZWrite),
        static_cast<unsigned long>(diagnostic.lastZFunc),static_cast<unsigned long long>(diagnostic.geometryRejected),diagnostic.nativeMarkerRejectStage);
    Check(left.pixels>20&&right.pixels>20,"original native-style sprite produces actual HAL pixels in both eyes");
    Check(!left.changedColour&&!right.changedColour,"native fixed shield vertex colour remains exact in both eye images, with no custom blue glyph");
    Check(left.centre-right.centre>2.5&&left.centre-right.centre<5.5,"production native icon centre follows actual target-depth stereo pose");
    Check(Snapshot().nativeMarkerRouted==2&&Snapshot().nativeMarkerDrawn==2&&Snapshot().nativeMarkerReprojected==2&&!Snapshot().nativeMarkerRejected,"actual Position/Emit/Draw/Indexed pipeline counters prove two translated native draws");
    Check(Snapshot().nativeDestinationDrawn==2,"original native fixed destination draws in both eyes instead of being suppressed for a custom glyph");
    const auto savedFrame=frame;const auto savedRecord=record;
    Put(uiCamera+0x3C,4.f/3.f);frame.projection[0]=.93f;frame.projection[5]=1.12f;
    std::memcpy(frame.referenceProjection,frame.projection,sizeof(frame.projection));++frame.pairId;
    const std::uint32_t goalType=26;std::memcpy(record.data()+0x1C,&goalType,4);record[0x4D]=0;record[0x4F]=1;
    NativeCameraMath::Vec3 stableGoal{250,0,0};std::memcpy(record.data()+4,&stableGoal,sizeof(stableGoal));
    verifyRoundGeometry=true;const auto beforeGoal=Snapshot();
    for(unsigned eye=0;eye<2;++eye){float bob=eye?12.f:5.f;std::memcpy(record.data()+0x40,&bob,4);
        BeginNativeMarkers(frame.player,reinterpret_cast<void*>(0x654321),reinterpret_cast<void*>(nativeCamera));MarkerPositionHook(record.data(),nullptr,&position);
        Check(position.y==bob,"native gameplay position retains its authored bob without actor/marker writes");
        emitMarker();EndNativeMarkers();MarkerPacket goal;Check(MatchMarker(entry,goal)&&goal.target.y==0,"VR objective mirror independently removes native bob from the actual source point");
        frame.actualEye.position={0,0,eye?-3.2f:3.2f};Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear circular objective eye");
        SpriteDrawHook(binder,nullptr,nullptr);Check(Pixels(backbuffer).pixels>20,"actual circular objective retains both eye pixel coverage");}
    verifyRoundGeometry=false;
    Check(Snapshot().nativeObjectiveBobSuppressed==beforeGoal.nativeObjectiveBobSuppressed+2&&Snapshot().nativeObjectivePairsReused==beforeGoal.nativeObjectivePairsReused+1,
          "production source and draw diagnostics prove stable native objective point across both eyes");
    frame=savedFrame;record=savedRecord;Put(uiCamera+0x3C,1.f);
    BeginNativeMarkers(frame.player,reinterpret_cast<void*>(0x654321),reinterpret_cast<void*>(nativeCamera));MarkerPositionHook(record.data(),nullptr,&position);emitMarker();EndNativeMarkers();
    // Regression from the actual .34 PID33584, where every one of6197
    // captured native draws failed the old z6 UI gate. Only the recorded
    // depth/rows are replayed; corner spans follow the proved4996BF writer.
    const float observedRows[]={.7881976f,0,.0699268f,0,0,.7361624f,.03524249f,0,0,0,1,-5,0,0,1,0};
    for(float depth:{-2140.42f,1322.21f,1.f,0.f}){
        NativeHudMath::Vertex observedVertices[4];std::memcpy(observedVertices,vertices,sizeof(vertices));
        for(unsigned i=0;i<4;++i){observedVertices[i].x=(i==0||i==3)?-140.f:140.f;observedVertices[i].y=i<2?-140.f:140.f;observedVertices[i].z=depth;}
        Hr(vb->Lock(0,sizeof(observedVertices),&memory,0),"write source-contract observed native world quad");std::memcpy(memory,observedVertices,sizeof(observedVertices));vb->Unlock();
        Check(shadow.Commit(va,shadow.Begin(va),observedVertices,sizeof(observedVertices)),"capture variable-Z world marker through exact native writer shadow");
        Hr(device->SetVertexShaderConstantF(0,observedRows,4),"replay retained sheared native world projection");
        Coverage eyes[2];const auto before=Snapshot().nativeMarkerReprojected,negative=Snapshot().nativeMarkerNegativeW;
        for(unsigned eye=0;eye<2;++eye){frame.actualEye.position={0,0,eye?-3.2f:3.2f};
            Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear observed native world marker eye");SpriteDrawHook(binder,nullptr,nullptr);eyes[eye]=Pixels(backbuffer);
            Hr(device->GetVertexShaderConstantF(0,restored,4),"read variable-Z native rows after draw");Check(!std::memcmp(observedRows,restored,sizeof(restored)),"observed native rows restored bit exactly after either eye");}
        std::printf("HAL replay native Z %.2f pixels %u / %u; disparity %.3f px.\n",depth,eyes[0].pixels,eyes[1].pixels,eyes[0].centre-eyes[1].centre);
        Check(eyes[0].pixels>20&&eyes[1].pixels>20&&eyes[0].centre-eyes[1].centre>2.5&&eyes[0].centre-eyes[1].centre<5.5,
              "actual retained variable-Z/sheared rows yield target-aligned stereo HAL pixels");
        Check(eyes[0].pixels==left.pixels&&eyes[1].pixels==right.pixels,"producer authored native size is stable across source depth and both eyes, without W size pumping");
        Check(Snapshot().nativeMarkerReprojected==before+2&&Snapshot().nativeMarkerVertexCount==4&&Snapshot().nativeMarkerMinZ==depth&&Snapshot().nativeMarkerMinW==depth&&
              Snapshot().nativeMarkerNegativeW==negative+(depth<0?2:0),"actual production route diagnoses source depth and normalizes only negative native W");
    }
    Hr(vb->Lock(0,sizeof(vertices),&memory,0),"restore original native test vertices");std::memcpy(memory,vertices,sizeof(vertices));vb->Unlock();
    Check(shadow.Commit(va,shadow.Begin(va),vertices,sizeof(vertices)),"restore source shadow for baseline failure tests");Hr(device->SetVertexShaderConstantF(0,nativeRows,4),"restore baseline native rows");
    std::array<unsigned char,0x64> recordB=record;nativePosition={250,-30,60};std::memcpy(recordB.data()+4,&nativePosition,sizeof(nativePosition));
    BeginNativeMarkers(frame.player,reinterpret_cast<void*>(0x654321),reinterpret_cast<void*>(nativeCamera));MarkerPositionHook(record.data(),nullptr,&position);emitMarker();
    MarkerPositionHook(recordB.data(),nullptr,&position);emitMarker();EndNativeMarkers();
    MarkerPacket packet;Check(MatchMarker(entry,packet)&&packet.ambiguous&&Snapshot().nativeMarkerAmbiguous==1,"two distinct native records in same stamped entry are never translated as one last target");
    const auto beforeAmbiguous=Snapshot().nativeMarkerReprojected;const auto callsBeforeAmbiguous=nativeDrawCalls;
    SpriteDrawHook(binder,nullptr,nullptr);Check(Snapshot().nativeMarkerReprojected==beforeAmbiguous&&nativeDrawCalls==callsBeforeAmbiguous&&Snapshot().nativeMarkerRejectStage==45,"ambiguous proven marker is hidden rather than drawn at a wrong native screen location");
    const auto entryA=entry,entryB=image+0x11000;Put(entryB,std::uint32_t(image+0x540EAC));Put(entryB+0x100,std::uint32_t(image+0x21000));Put(entryB+0x130,std::uint32_t(71));Put(entryB+0x108,material);
    BeginNativeMarkers(frame.player,reinterpret_cast<void*>(0x654321),reinterpret_cast<void*>(nativeCamera));MarkerPositionHook(record.data(),nullptr,&position);emitMarker();
    entry=entryB;MarkerPositionHook(recordB.data(),nullptr,&position);emitMarker();EndNativeMarkers();entry=entryA;
    MarkerPacket a,b;Check(MatchMarker(entryA,a)&&MatchMarker(entryB,b)&&!a.ambiguous&&!b.ambiguous&&a.record!=b.record&&a.target.z==0&&b.target.z==60,
                         "same texture with separate native entries keeps each record and target, and a new producer clears old ambiguity");
    Coverage distinct[2];for(unsigned marker=0;marker<2;++marker){binder[3]=static_cast<std::uint32_t>(marker?entryB:entryA);
        Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear distinct native marker target");SpriteDrawHook(binder,nullptr,nullptr);distinct[marker]=Pixels(backbuffer);}
    Check(distinct[0].pixels>20&&distinct[1].pixels>20&&distinct[0].centre-distinct[1].centre>30,"two separately emitted native markers draw at their own world points rather than the last record");binder[3]=static_cast<std::uint32_t>(entryA);
    // Objective textures24/25 use the same owned producer but a type6 quad
    // and a separately emitted cropped foreground. Keep its authored offset.
    auto objectiveMaterial=std::array<std::uint32_t,5>{1,32,32,6,0};
    const auto objectiveEntry=image+0x12000,foregroundEntry=image+0x13000;
    for(auto item:{objectiveEntry,foregroundEntry}){Put(item,std::uint32_t(image+0x540EAC));Put(item+0x100,std::uint32_t(image+0x22000));Put(item+0x130,std::uint32_t(71));Put(item+0x108,objectiveMaterial);}
    authored={};authored[23]=authored[24]=.6f;
    const float corners[4][2]={{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}};
    for(unsigned i=0;i<4;++i){authored[11+3*i]=corners[i][0];authored[12+3*i]=corners[i][1];}
    BeginNativeMarkers(frame.player,reinterpret_cast<void*>(0x654321),reinterpret_cast<void*>(nativeCamera));MarkerPositionHook(record.data(),nullptr,&position);entry=objectiveEntry;emitMarker();
    authored[12]=authored[15]=0;entry=foregroundEntry;emitMarker();EndNativeMarkers();entry=entryA;
    MarkerPacket objective,foreground;Check(MatchMarker(objectiveEntry,objective)&&MatchMarker(foregroundEntry,foreground)&&objective.layout.valid&&foreground.layout.valid&&
        std::fabs(objective.layout.width-.06f)<1e-6f&&std::fabs(foreground.layout.height-.03f)<1e-6f&&std::fabs(foreground.layout.offsetY-.015f)<1e-6f,
        "objective native outer and cropped foreground retain authored size and offset");
    Coverage objectiveEye[2];for(unsigned eye=0;eye<2;++eye){frame.actualEye.position={0,0,eye?-3.2f:3.2f};binder[3]=static_cast<std::uint32_t>(objectiveEntry);
        Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear objective actual eye");SpriteDrawHook(binder,nullptr,nullptr);objectiveEye[eye]=Pixels(backbuffer);}
    Check(objectiveEye[0].pixels==objectiveEye[1].pixels&&objectiveEye[0].pixels==left.pixels&&objectiveEye[0].centre-objectiveEye[1].centre>2.5,
          "objective native type6 image has identical native size in both eyes with actual world disparity");
    binder[3]=static_cast<std::uint32_t>(foregroundEntry);Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear cropped objective foreground");SpriteDrawHook(binder,nullptr,nullptr);
    Check(Pixels(backbuffer).pixels<objectiveEye[1].pixels,"cropped objective foreground does not inflate or recenter into full duplicate icon");
    authored={};authored[7]=authored[8]=.6f;binder[3]=static_cast<std::uint32_t>(entryA);
    const auto translated=Snapshot().nativeMarkerReprojected;
    const auto savedBackend=backend.device;backend.device=0;
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear missing-backend-proof fallback image");const auto beforeBackend=nativeDrawCalls;
    SpriteDrawHook(binder,nullptr,nullptr);backend.device=savedBackend;
    Check(nativeDrawCalls==beforeBackend&&Pixels(backbuffer).pixels==0&&Snapshot().nativeMarkerReprojected==translated,
          "unavailable backend hides only the proven marker instead of reverting to displaced screen coordinates");
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear viewport-failure fallback image");
    frame.width=319;const auto beforeViewport=nativeDrawCalls;SpriteDrawHook(binder,nullptr,nullptr);frame.width=320;
    Check(nativeDrawCalls==beforeViewport&&Pixels(backbuffer).pixels==0&&Snapshot().nativeMarkerReprojected==translated,
          "unavailable eye viewport hides the proven world marker without producing duplicate native pixels");
    const RECT full{0,0,320,240};Hr(device->SetScissorRect(&full),"set original native scissor rectangle");
    Hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"enable original native scissor");
    auto** originalTable=*reinterpret_cast<void***>(device);std::array<void*,119> faultTable;
    // A private COM forwarding facade injects one failure. The actual driver's
    // object/vtable remain untouched; each stub substitutes only its this arg.
    for(unsigned i=0;i<faultTable.size();++i){auto* code=privateImage+0x50000+i*16;
        const unsigned char prefix[]={0xC7,0x44,0x24,4};std::memcpy(code,prefix,4);
        Put(reinterpret_cast<std::uintptr_t>(code)+4,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(device)));
        code[8]=0xB8;Put(reinterpret_cast<std::uintptr_t>(code)+9,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(originalTable[i])));
        code[13]=0xFF;code[14]=0xE0;faultTable[i]=code;}
    FlushInstructionCache(GetCurrentProcess(),privateImage+0x50000,119*16);
    originalSetState=reinterpret_cast<SetState>(originalTable[57]);faultTable[57]=reinterpret_cast<void*>(&FaultState);
    DeviceProxy proxy{faultTable.data(),device};backend.device=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&proxy));
    auto proxyVA=va,proxyIA=ia;proxyVA.identity.device=proxyIA.identity.device=reinterpret_cast<std::uintptr_t>(&proxy);
    Check(shadow.Commit(proxyVA,shadow.Begin(proxyVA),vertices,sizeof(vertices))&&shadow.Commit(proxyIA,shadow.Begin(proxyIA),indices,sizeof(indices)),"private COM fault facade retains exact vertex/index shadow identity");
    failScissor=requireOriginalState=true;const auto beforeLate=nativeDrawCalls;
    Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear late-state-failure fallback image");SpriteDrawHook(binder,nullptr,nullptr);
    backend.device=savedBackend;requireOriginalState=false;
    Check(!failScissor&&nativeDrawCalls==beforeLate&&Pixels(backbuffer).pixels==0&&Snapshot().nativeMarkerReprojected==translated,
          "late state failure restores device state and suppresses the wrongly positioned native fallback");
    shadow.Clear();Hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"clear missing-shadow fallback image");
    const auto beforeShadow=nativeDrawCalls;SpriteDrawHook(binder,nullptr,nullptr);
    Check(nativeDrawCalls==beforeShadow&&Pixels(backbuffer).pixels==0&&Snapshot().nativeMarkerReprojected==translated,
          "missing marker shadow cannot display a giant native fallback");
    SetReticleEnabled(true);SetReticleProvider(Provider);reticleDrawScope.player=frame.player;
    const auto beforeReticle=nativeDrawCalls;DrawReticle(&backend,D3DPT_TRIANGLELIST,0,0,4,0,2);
    Check(nativeDrawCalls==beforeReticle,"existing ordinary reticle proof failure remains suppressed");SetReticleEnabled(false);SetReticleProvider(nullptr);
    Put(entry+0x130,std::uint32_t(72));MarkerPacket unmatched;Check(!MatchMarker(entry,unmatched),"recycled native packet stamp cannot reuse the previous world target");
    Put(entry+0x130,std::uint32_t(71));tick=1251;Check(!MatchMarker(entry,unmatched),"expired native sprite cannot retain old target");
    SetNativeMarkerProvider(nullptr);InvalidateDevice();verifyNativeResources=false;device->SetTexture(0,nullptr);Release(expectedNativeTexture);Release(ib);Release(vb);Release(declaration);Release(ps);Release(vs);Release(backbuffer);Release(device);Release(api);DestroyWindow(window);VirtualFree(privateImage,0,MEM_RELEASE);
    std::printf("Native marker production routing: %u checks PASS (HAL).\n",checks);return 0;
}
