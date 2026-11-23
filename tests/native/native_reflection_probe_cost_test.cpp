// Actual production observer + actual native cinematic pointer walk. Private
// process memory and a COM getter returning no shader; no game/window/GPU.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace CostFixture {inline DWORD tick=1000;inline DWORD WINAPI Clock(){return tick;}}
#define GetTickCount CostFixture::Clock
#include "native_reflection_probe_kl.cpp"
#undef GetTickCount
#include "native_cinematic_kl.hpp"
#include <cstdio>
#include <cstdlib>
namespace CostFixture {
unsigned predicates=0,reads=0;std::uintptr_t image=0;
bool Scene(){
    ++predicates;
    const auto state=NativeCinematicKL::Snapshot(image,[](std::uintptr_t at,void* out,std::size_t size){
        ++reads;SIZE_T got=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;
    });
    return state.valid&&!NativeCinematicKL::FullScenePanel(state,true);
}
struct Device {void** table=nullptr;unsigned getters=0;};
HRESULT STDMETHODCALLTYPE Pixel(Device* self,IDirect3DPixelShader9** out){++self->getters;*out=nullptr;return S_OK;}
HRESULT STDMETHODCALLTYPE Vertex(Device* self,IDirect3DVertexShader9** out){++self->getters;*out=nullptr;return S_OK;}
[[noreturn]]void Unexpected(){std::puts("FAIL unexpected COM method");std::abort();}
void Check(bool value,const char* message){if(!value){std::printf("FAIL %s\n",message);std::exit(1);}}
void U32(std::uintptr_t at,std::uint32_t value){std::memcpy(reinterpret_cast<void*>(at),&value,4);}
}
int main(){using namespace CostFixture;using namespace NativeReflectionProbeKL;
    image=0x25000000;
    void* memory=VirtualAlloc(reinterpret_cast<void*>(image),0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    Check(memory==reinterpret_cast<void*>(image),"private image allocation");
    const auto system=image+0x10000,manager=image+0x20000;
    U32(image+0x6875F0,system);U32(system+0x96C,manager);U32(manager+0x300,0);U32(image+0x88E8B0,0);
    void* methods[119];for(auto& method:methods)method=reinterpret_cast<void*>(&Unexpected);
    methods[108]=reinterpret_cast<void*>(&Pixel);methods[93]=reinterpret_cast<void*>(&Vertex);Device fixture{methods};
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&fixture);
    LARGE_INTEGER frequency{},start{},eager{},deferred{};QueryPerformanceFrequency(&frequency);
    Configure(true);Reset();predicates=reads=0;QueryPerformanceCounter(&start);
    for(unsigned draw=0;draw<40000;++draw)if(Scene())Observe(device,1);
    QueryPerformanceCounter(&eager);
    Check(predicates==40000&&reads==160000&&fixture.getters==2&&Snapshot().samples==1,
        "old eager scene callback performs actual 160000 native reads for one diagnostic sample");
    const auto eagerPredicates=predicates,eagerReads=reads;const auto oldGets=fixture.getters;
    Reset();tick+=100;predicates=reads=0;
    for(unsigned draw=0;draw<40000;++draw)Observe(device,2,Scene);
    QueryPerformanceCounter(&deferred);
    Check(predicates==1&&reads==4&&fixture.getters==oldGets+2&&Snapshot().samples==1,
        "deferred production callback retains the same sample with four native reads");
    std::printf("Reflection probe 40000-draw private callback fixture: eager %.3f ms/%u predicates/%u native reads; deferred %.3f ms/%u predicates/%u native reads; same sample count. Not gameplay FPS.\n",
        1000.*double(eager.QuadPart-start.QuadPart)/frequency.QuadPart,eagerPredicates,eagerReads,
        1000.*double(deferred.QuadPart-eager.QuadPart)/frequency.QuadPart,predicates,reads);
    Configure(false);VirtualFree(memory,0,MEM_RELEASE);return 0;
}
