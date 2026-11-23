#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "native_cinema_viewport.hpp"
#include <cstdio>
#include <cstdlib>
#include <array>
namespace {unsigned checks=0;void Check(bool ok,const char* text){++checks;if(!ok){std::printf("FAIL: %s\n",text);std::exit(1);}}}
namespace {
struct Surface {void** table=nullptr;} surface;
unsigned targetWidth=640,targetHeight=480;
IDirect3DDevice9* forwardedDevice=nullptr;
bool failNextScissor=false;
ULONG STDMETHODCALLTYPE ReleaseSurface(IDirect3DSurface9*){return 1;}
HRESULT STDMETHODCALLTYPE Describe(IDirect3DSurface9*,D3DSURFACE_DESC* desc){*desc={};desc->Width=targetWidth;desc->Height=targetHeight;desc->Format=D3DFMT_A8R8G8B8;return S_OK;}
HRESULT STDMETHODCALLTYPE Target(IDirect3DDevice9*,DWORD index,IDirect3DSurface9** out){if(index||!out)return E_FAIL;*out=reinterpret_cast<IDirect3DSurface9*>(&surface);return S_OK;}
HRESULT STDMETHODCALLTYPE Scissor(IDirect3DDevice9*,const RECT* rect){if(failNextScissor){failNextScissor=false;return E_FAIL;}return forwardedDevice->SetScissorRect(rect);}
struct Proxy {void** table=nullptr;};
float ScreenY(const float* wvp,const D3DVIEWPORT9& viewport,float x,float y,float z){
    // m4x4 oPos,v0,c8 dot-products shader rows: native Common transposes
    // its row-vector combined matrix before uploading these constants.
    const auto clipY=x*wvp[4]+y*wvp[5]+z*wvp[6]+wvp[7];
    const auto clipW=x*wvp[12]+y*wvp[13]+z*wvp[14]+wvp[15];
    return viewport.Y+(1.f-clipY/clipW)*viewport.Height*.5f;
}
void CombinedWvp(const float* projection,float eyeX,float* result){
    // Row-vector world/view followed by P; translation is deliberately
    // nonzero so this is a real combined mesh matrix, never standalone P.
    for(unsigned i=0;i<16;++i)result[i]=projection[i];
    for(unsigned col=0;col<4;++col)result[12+col]=
        -eyeX*projection[col]+8.f*projection[8+col]+projection[12+col];
}
}
int main(){
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KL1PrivateCinemaViewport";RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    Check(window!=nullptr,"create hidden private viewport fixture window");
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"private D3D9 API");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    IDirect3DDevice9* device=nullptr;
    Check(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device)),"private NULLREF device (no game/HMD)");
    // NULLREF retains real viewport/constants/state but supplies no physical
    // render target. Substitute only its descriptor; all other methods call
    // the actual private device. No user's driver vtable is patched.
    auto* realDevice=device;forwardedDevice=realDevice;std::array<void*,13> surfaceTable{};surfaceTable[2]=reinterpret_cast<void*>(ReleaseSurface);surfaceTable[12]=reinterpret_cast<void*>(Describe);surface.table=surfaceTable.data();
    auto** original=*reinterpret_cast<void***>(realDevice);std::array<void*,119> table;
    auto* stubs=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));Check(stubs!=nullptr,"private COM forwarding stubs");
    for(unsigned i=0;i<table.size();++i){auto* code=stubs+i*16;const unsigned char prefix[]={0xC7,0x44,0x24,4};std::memcpy(code,prefix,4);
        const auto object=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(realDevice)),method=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(original[i]));
        std::memcpy(code+4,&object,4);code[8]=0xB8;std::memcpy(code+9,&method,4);code[13]=0xFF;code[14]=0xE0;table[i]=code;}
    table[38]=reinterpret_cast<void*>(Target);table[75]=reinterpret_cast<void*>(Scissor);Proxy proxy{table.data()};device=reinterpret_cast<IDirect3DDevice9*>(&proxy);FlushInstructionCache(GetCurrentProcess(),stubs,4096);
    float projection[16]{};const float fov[]={1,1,1,1};Check(NativeCameraMath::ApplyEyeProjection(projection,fov,1,40000,false),"SDK perspective fixture");
    Check(SUCCEEDED(device->SetVertexShaderConstantF(24,projection,4)),"scene projection upload");
    Check(SUCCEEDED(device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE))&&SUCCEEDED(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL)),"world depth semantics");
    const D3DVIEWPORT9 letterbox{0,100,640,280,0,1};Check(SUCCEEDED(device->SetViewport(&letterbox)),"native authored letterbox viewport");
    {D3DVIEWPORT9 vp{};DWORD z=0,func=0;float raw[16]{};IDirect3DSurface9* rt=nullptr;D3DSURFACE_DESC desc{};
        device->GetViewport(&vp);device->GetRenderState(D3DRS_ZENABLE,&z);device->GetRenderState(D3DRS_ZFUNC,&func);
        device->GetVertexShaderConstantF(24,raw,4);device->GetRenderTarget(0,&rt);if(rt){rt->GetDesc(&desc);rt->Release();}
        std::printf("private viewport %lu,%lu %lux%lu target%ux%u z%lu func%lu projection%g,%g,%g,%g\n",vp.X,vp.Y,vp.Width,vp.Height,desc.Width,desc.Height,z,func,raw[0],raw[5],raw[11],raw[14]);}
    NativeCinemaViewport::Scope scope;D3DVIEWPORT9 actual{};
    Check(scope.Begin(device,true,640,480,projection),"owned SDK scene expands centred letterbox to full target");
    device->GetViewport(&actual);Check(actual.Y==0&&actual.Height==480&&actual.Width==640,"full vertical image uses SDK FOV without vertical compression");
    Check(scope.Finish(),"original viewport restored after draw");device->GetViewport(&actual);Check(!std::memcmp(&actual,&letterbox,sizeof(actual)),"all original viewport/depth bytes restored");
    Check(!scope.Begin(device,false,640,480,projection),"ordinary gameplay never changes native viewport");
    Check(!scope.Begin(device,true,320,240,projection),"foreign/smaller render target rejected");
    device->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);Check(!scope.Begin(device,true,640,480,projection),"native screen HUD/postprocessing stays untouched");
    device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);float other[16]{};
    Check(!scope.Begin(device,true,640,480,nullptr),"missing applied SDK projection cannot expand viewport");
    Check(!scope.Begin(device,true,640,480,other),"orthographic/invalid scene projection rejected");
    auto crop=letterbox;crop.Y=30;device->SetViewport(&crop);Check(!scope.Begin(device,true,640,480,projection),"off-centre inset crop is not a native letterbox");
    crop.Y=0;device->SetViewport(&crop);Check(scope.Begin(device,true,640,480,projection),"owned cinematic top-aligned subviewport is expanded too");
    Check(scope.Finish(),"top-aligned viewport restores after its world draw");
    device->SetRenderState(D3DRS_COLORWRITEENABLE,0);Check(!scope.Begin(device,true,640,480,projection),"depth-only/shadow world draw is not expanded");
    device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    // Reproduce the retained .39 stage6 failure: full-size scene target with
    // a 3840x1227 centred native viewport, ordinary WVP-only mesh shader and
    // NO standalone P at c0/c24/c69. Each eye still has its own asymmetric P.
    const DWORD meshShader[]={0xfffe0200,0x0200001f,0x80000000,0x900f0000,
        0x03000014,0xc00f0000,0x90e40000,0xa0e40008,0x0000ffff};
    IDirect3DVertexShader9* mesh=nullptr;Check(SUCCEEDED(device->CreateVertexShader(meshShader,&mesh)),"real WVP-only mesh vertex shader");
    Check(SUCCEEDED(device->SetVertexShader(mesh)),"bind WVP-only mesh program");
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration=nullptr;
    Check(SUCCEEDED(device->CreateVertexDeclaration(elements,&declaration))&&SUCCEEDED(device->SetVertexDeclaration(declaration)),"actual WVP mesh input declaration");
    float zero[16]{};for(unsigned reg:{0u,24u,69u})device->SetVertexShaderConstantF(reg,zero,4);
    targetWidth=3840;targetHeight=2160;
    const D3DVIEWPORT9 retained{0,466,3840,1227,0,1};
    const RECT nativeScissor{0,466,3840,1693};
    float eyeWvp[2][16]{};
    for(unsigned eye=0;eye<2;++eye){
        const float eyeFov[]={eye?.9f:1.1f,eye?1.1f:.9f,1.f,1.f};float eyeProjection[16]{};
        Check(NativeCameraMath::ApplyEyeProjection(eyeProjection,eyeFov,1,40000,false),"asymmetric eye SDK projection");
        float rowWvp[16]{};CombinedWvp(eyeProjection,eye?.032f:-.032f,rowWvp);
        for(unsigned i=0;i<16;++i)eyeWvp[eye][i]=rowWvp[(i%4)*4+i/4];
        Check(SUCCEEDED(device->SetVertexShaderConstantF(8,eyeWvp[eye],4)),"upload combined per-eye world/view/projection only");
        Check(SUCCEEDED(device->SetViewport(&retained))&&SUCCEEDED(device->SetScissorRect(&nativeScissor))&&
            SUCCEEDED(device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE)),"retained cinema viewport and matching native scissor");
        Check(scope.Begin(device,true,3840,2160,eyeProjection),"WVP-only actual cinema world draw reaches repair (old stage6 rejected)");
        device->GetViewport(&actual);RECT rect{};device->GetScissorRect(&rect);float shaderWvp[16]{};
        device->GetVertexShaderConstantF(8,shaderWvp,4);
        Check(actual.Y==0&&actual.Height==2160&&rect.top==0&&rect.bottom==2160,"viewport and matching scissor expose complete VR vertical field");
        Check(!std::memcmp(shaderWvp,eyeWvp[eye],sizeof(shaderWvp)),"combined stereo WVP coefficients are preserved exactly");
        Check(std::fabs(ScreenY(shaderWvp,actual,0,8,0))<.01f&&std::fabs(ScreenY(shaderWvp,actual,0,-8,0)-2160)<.01f,
            "SDK upper/lower world rays map to target edges rather than squashed inset");
        const float vertices[][3]={{-1,-1,0},{0,1,0},{1,-1,0}};
        Check(SUCCEEDED(device->BeginScene()),"begin private mesh scene");
        const auto draw=device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]));
        const auto end=device->EndScene();Check(SUCCEEDED(draw)&&SUCCEEDED(end),"execute actual WVP-only private draw inside repaired viewport/scissor");
        Check(scope.Finish(),"WVP draw restores scoped viewport and scissor");
        device->GetViewport(&actual);device->GetScissorRect(&rect);
        Check(!std::memcmp(&actual,&retained,sizeof(actual))&&!std::memcmp(&rect,&nativeScissor,sizeof(rect)),"exact authored viewport and scissor restored before next native pass");
    }
    Check(eyeWvp[0][2]!=eyeWvp[1][2]&&eyeWvp[0][3]!=eyeWvp[1][3],"eyes retain separate SDK asymmetry and parallax");
    device->SetViewport(&retained);const RECT materialClip{80,500,3100,1600};device->SetScissorRect(&materialClip);
    Check(scope.Begin(device,true,3840,2160,projection),"world viewport accepts a separate material clip");
    RECT rect{};device->GetScissorRect(&rect);Check(!std::memcmp(&rect,&materialClip,sizeof(rect)),"unrelated material scissor is never enlarged");scope.Finish();
    device->SetScissorRect(&nativeScissor);failNextScissor=true;
    Check(!scope.Begin(device,true,3840,2160,projection),"failed matching-scissor mutation rejects adapter");
    device->GetViewport(&actual);Check(!std::memcmp(&actual,&retained,sizeof(actual))&&scope.Finish(),"failed setup rolls back native viewport immediately");
    auto full=retained;full.Y=0;full.Height=2160;device->SetViewport(&full);
    Check(!scope.Begin(device,true,3840,2160,projection),"ordinary full-size VR cinema draw remains untouched");
    auto half=retained;half.Y=0;half.Width=1920;half.Height=1080;device->SetViewport(&half);
    Check(!scope.Begin(device,true,3840,2160,projection),"retained half-size postprocess pass is never enlarged");
    device->SetViewport(&retained);targetWidth=256;targetHeight=256;
    Check(!scope.Begin(device,true,3840,2160,projection),"shadow target rejected even when viewport and cached SDK P look cinematic");
    declaration->Release();mesh->Release();
    realDevice->Release();VirtualFree(stubs,0,MEM_RELEASE);api->Release();DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::printf("Native cinematic viewport: %u checks PASS (private NULLREF state, target descriptor fixture).\n",checks);
}
