#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include "d3d9_on12_api.hpp"
#include "d3d9_normalization.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
template<class T> struct Object {
    T* value=nullptr;
    ~Object(){if(value)value->Release();}
    T** Put(){return &value;}
    T* operator->()const{return value;}
};
bool Hr(HRESULT result,const char* operation){
    if(SUCCEEDED(result))return true;
    std::printf("FAIL: %s (HRESULT 0x%08lx).\n",operation,static_cast<unsigned long>(result));return false;
}
bool CopyFixture(IDirect3DDevice9* device,unsigned width,unsigned height,D3DMULTISAMPLE_TYPE samples){
    Object<IDirect3DSurface9> source,nativeResolve,bgra,readback,previous;
    Object<IDirect3DTexture9> texture;
    if(!Hr(device->CreateRenderTarget(width,height,D3DFMT_X8R8G8B8,samples,0,FALSE,source.Put(),nullptr),"create native X8 target")||
       !Hr(device->CreateRenderTarget(width,height,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,nativeResolve.Put(),nullptr),"create same-format resolve")||
       !Hr(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,texture.Put(),nullptr),"create native-size BGRA texture")||
       !Hr(texture->GetSurfaceLevel(0,bgra.Put()),"get BGRA surface")||
       !Hr(device->GetRenderTarget(0,previous.Put()),"save native target")||
       !Hr(device->SetRenderTarget(0,source.value),"bind native target"))return false;
    const auto clear=device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff336699,1.f,0);
    const auto restored=device->SetRenderTarget(0,previous.value);
    if(!Hr(clear,"clear native X8 target")||!Hr(restored,"restore native target"))return false;
    D3DSURFACE_DESC description{},output{};
    if(!Hr(source->GetDesc(&description),"read native descriptor")||!Hr(bgra->GetDesc(&output),"read BGRA descriptor"))return false;
    if(description.Width!=output.Width||description.Height!=output.Height){std::puts("FAIL: dimensions changed during normalization.");return false;}
    if(D3D9Normalization::NeedsNativeResolve(description)!=(samples!=D3DMULTISAMPLE_NONE)){std::puts("FAIL: wrong resolve policy.");return false;}
    D3D9Normalization::Step step{};
    if(!Hr(D3D9Normalization::Copy(device,source.value,description,nativeResolve.value,bgra.value,step),"production exact-size normalization")||
       !Hr(device->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,readback.Put(),nullptr),"create verification readback")||
       !Hr(device->GetRenderTargetData(bgra.value,readback.value),"read normalized pixels"))return false;
    D3DLOCKED_RECT lock{};if(!Hr(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY),"lock verification pixels"))return false;
    bool pixels=true;
    for(unsigned y=0;y<height&&pixels;++y)for(unsigned x=0;x<width;++x){
        DWORD pixel=0;std::memcpy(&pixel,static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch+x*4,4);
        if((pixel&0xffffff)!=0x336699){pixels=false;break;}
    }
    readback->UnlockRect();if(!pixels){std::puts("FAIL: normalized RGB pixels differ from native target.");return false;}
    std::printf("PASS: exact %ux%u X8 %s -> A8 pixels.\n",width,height,samples==D3DMULTISAMPLE_NONE?"single-sample":"MSAA resolve");return true;
}
}
int main(int argc,char** argv){
    const bool on12=argc==2&&!std::strcmp(argv[1],"--on12");
    if(argc>2||(argc==2&&!on12))return 2;
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Kl1NormalizationFixture";
    if(!RegisterClassW(&cls))return 2;
    const auto window=CreateWindowW(cls.lpszClassName,L"KL1 normalization fixture",WS_OVERLAPPEDWINDOW,0,0,128,96,nullptr,nullptr,cls.hInstance,nullptr);
    const auto library=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    int result=0;
    {
        Object<IDirect3D9> api;Object<IDirect3DDevice9> device;
        if(library&&on12){using Factory=IDirect3D9*(WINAPI*)(UINT,Args9On12*,UINT);
            const auto factory=reinterpret_cast<Factory>(GetProcAddress(library,"Direct3DCreate9On12"));Args9On12 args{};args.enable=TRUE;
            if(factory)api.value=factory(D3D_SDK_VERSION,&args,1);
        }else if(library){using Factory=IDirect3D9*(WINAPI*)(UINT);const auto factory=reinterpret_cast<Factory>(GetProcAddress(library,"Direct3DCreate9"));if(factory)api.value=factory(D3D_SDK_VERSION);}
        if(!window||!api.value){std::puts("SKIP: requested system D3D9 factory unavailable.");result=0;}
        else {
            D3DPRESENT_PARAMETERS parameters{};parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
            parameters.hDeviceWindow=window;parameters.BackBufferWidth=128;parameters.BackBufferHeight=96;parameters.BackBufferFormat=D3DFMT_X8R8G8B8;
            const auto created=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&parameters,device.Put());
            if(FAILED(created))std::printf("SKIP: %s HAL unavailable (0x%08lx).\n",on12?"D3D9On12":"D3D9",static_cast<unsigned long>(created));
            else if(FAILED(api->CheckDeviceFormatConversion(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_A8R8G8B8)))std::puts("SKIP: driver does not support native X8 -> A8 conversion.");
            else {
                std::printf("Testing system %s normalization; hidden window, no Present or VR session.\n",on12?"D3D9On12":"D3D9");
                if(!CopyFixture(device.value,64,48,D3DMULTISAMPLE_NONE))result=1;
                bool supported=false;
                for(const auto samples:{D3DMULTISAMPLE_2_SAMPLES,D3DMULTISAMPLE_4_SAMPLES}){
                    DWORD quality=0;if(FAILED(api->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,TRUE,samples,&quality))||!quality)continue;
                    supported=true;if(!CopyFixture(device.value,96,64,samples))result=1;
                }
                if(!supported)std::puts("SKIP: MSAA X8 render targets unsupported; only single-sample conversion verified.");
            }
        }
    }
    if(library)FreeLibrary(library);if(window)DestroyWindow(window);UnregisterClassW(cls.lpszClassName,cls.hInstance);return result;
}
