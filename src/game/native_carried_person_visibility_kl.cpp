#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_carried_person_visibility_kl.hpp"
#include "native_carried_person_visibility_kl_image.hpp"
#include "native_carried_person_visibility_kl_math.hpp"
#include "native_camera_kl.hpp"
#include "native_hands_kl.hpp"
#include "native_shader_constant_map.hpp"
#include "profile.hpp"
#include <array>
#include <atomic>
#include <vector>
namespace NativeCarriedPersonVisibilityKL {namespace {
namespace I=NativeCarriedPersonVisibilityKLImage;
std::uintptr_t image=0;std::atomic<bool> installed{false};
std::atomic<std::uint64_t> frames{0},carrying{0},candidates{0},clipped{0},restored{0},foreign{0},identityRejected{0},shaderRejected{0},unsupported{0},failed{0};
bool Read(std::uintptr_t at,void* out,std::size_t bytes){SIZE_T got=0;
    return NativeActionKL::Address(at,bytes)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,bytes,&got)&&got==bytes;}
struct Frame {bool active=false;I::Owner owner{};std::uintptr_t state=0;std::uint64_t epoch=0;float units=0;std::uint64_t serial=0;};
thread_local Frame frame;
thread_local std::uint64_t frameSerial=0;
struct Miss {std::uintptr_t entry=0,geometry=0,person=0,root=0,model=0;std::uint64_t epoch=0;};
thread_local std::array<Miss,1024> misses;
bool OwnCurrentEntry(std::uintptr_t entry){
    std::uint32_t geometry=0;if(!Read(entry+0xF4,&geometry,4)||!geometry)return false;
    auto& miss=misses[(entry>>4)&(misses.size()-1)];
    if(miss.entry==entry&&miss.geometry==geometry&&miss.person==frame.owner.person&&
       miss.root==frame.owner.root&&miss.model==frame.owner.model&&miss.epoch==frame.serial)return false;
    if(I::OwnEntry(image,entry,frame.owner,Read))return true;
    // Cache rejection only. A positive draw always re-proves the current
    // reciprocal actor relation; changed geometry/actor/eye misses this key.
    miss={entry,geometry,frame.owner.person,frame.owner.root,frame.owner.model,frame.serial};return false;
}
struct Shader {IDirect3DVertexShader9* source=nullptr;NativeShaderConstantMap::Range mvp{};};
struct Cache {
    IDirect3DDevice9* device=nullptr;unsigned planes=0;std::vector<Shader> shaders;
    void Clear(){for(auto& s:shaders)if(s.source)s.source->Release();shaders.clear();device=nullptr;planes=0;}
    ~Cache(){Clear();}
    bool Device(IDirect3DDevice9* next){
        if(device==next)return true;Clear();D3DCAPS9 caps{};
        if(FAILED(next->GetDeviceCaps(&caps)))return false;
        device=next;planes=caps.MaxUserClipPlanes<6?caps.MaxUserClipPlanes:6;return true;
    }
    NativeShaderConstantMap::Range MVP(IDirect3DDevice9* next){
        IDirect3DVertexShader9* source=nullptr;
        if(FAILED(next->GetVertexShader(&source))||!source)return {};
        for(auto& s:shaders)if(s.source==source){source->Release();return s.mvp;}
        Shader found;found.source=source;UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&!(bytes%4)){
            std::vector<std::uint32_t> code(bytes/4);NativeShaderConstantMap::Map map;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NativeShaderConstantMap::Parse(code.data(),bytes,map)&&map.mvp.count==4)found.mvp=map.mvp;
        }
        if(shaders.size()>=32){shaders.front().source->Release();shaders.erase(shaders.begin());}
        shaders.push_back(found);return found.mvp;
    }
};
thread_local Cache cache;
struct Scope {
    IDirect3DDevice9* device=nullptr;DWORD mask=0;unsigned index=0;float plane[4]{};bool active=false,broken=false;
    void Restore(){if(!active)return;active=false;
        const bool maskOK=SUCCEEDED(device->SetRenderState(D3DRS_CLIPPLANEENABLE,mask));
        const bool planeOK=SUCCEEDED(device->SetClipPlane(index,plane));
        if(maskOK&&planeOK)++restored;else{++failed;broken=true;}
    }
};
thread_local Scope scope;
thread_local bool entering=false;
}
bool Install(bool enabled,LogCallback log){
    if(installed)return true;if(!enabled)return false;
    wchar_t path[MAX_PATH]{};char digest[65]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH||!HashFile(path,digest)||!IsSupportedExecutableHash(digest))return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!I::Verify(image,Read)){if(log)log("Carried person visibility: native reciprocal carry graph signatures differ.");return false;}
    installed=true;if(log)log("Carried person visibility: complete main-eye clip for the exact locally carried actor; native shadows and other views preserved.");return true;
}
void SetFrame(std::uintptr_t player,float units,bool firstPerson){
    frame={};if(!installed||!firstPerson||!player||!std::isfinite(units)||units<1||units>1000)return;
    ++frames;const auto owner=I::Capture(image,player,Read);if(!owner.person)return;
    NativeCameraKL::EffectsView view;bool main=false;
    if(!NativeCameraKL::ReadEffectsView(view)||!view.frame||!view.state||
       !NativeCameraKL::DrawStateActive(view.state,&main)||!main)return;
    frame={true,owner,view.state,view.frame,units,++frameSerial};++carrying;
}
void ClearFrame(){scope.Restore();frame={};}
void* BeginDraw(IDirect3DDevice9* device){
    if(!device||!frame.active||scope.active||entering)return nullptr;
    struct Guard{Guard(){entering=true;}~Guard(){entering=false;}} guard;
    NativeCameraKL::EffectsView view;bool main=false;std::uintptr_t entry=0;unsigned pass=0;
    if(!NativeHandsKL::ReadCurrentNativeEntry(frame.state,entry,&pass)||pass==2||pass==3){++foreign;return nullptr;}
    if(!OwnCurrentEntry(entry)){++identityRejected;return nullptr;}
    if(!NativeCameraKL::ReadEffectsView(view)||view.state!=frame.state||view.frame!=frame.epoch||!view.worldKnown||
       !NativeCameraKL::DrawStateActive(view.state,&main)||!main){++foreign;return nullptr;}++candidates;
    if(!cache.Device(device)){++failed;return nullptr;}
    if(!cache.planes){++unsupported;return nullptr;}
    const auto mvp=cache.MVP(device);float rows[16]{};
    if(mvp.count!=4||FAILED(device->GetVertexShaderConstantF(mvp.start,rows,4))||
       !NativeCarriedPersonVisibilityKLMath::MatchesMVP(view.world,view.eye,view.projection,rows)){++shaderRejected;return nullptr;}
    float plane[4]{};if(!NativeCarriedPersonVisibilityKLMath::Plane(view.projection,frame.units,plane)){++shaderRejected;return nullptr;}
    DWORD mask=0;if(FAILED(device->GetRenderState(D3DRS_CLIPPLANEENABLE,&mask))){++failed;return nullptr;}
    unsigned index=0;while(index<cache.planes&&(mask&(1u<<index)))++index;
    if(index==cache.planes){++unsupported;return nullptr;}
    float saved[4]{};if(FAILED(device->GetClipPlane(index,saved))){++failed;return nullptr;}
    if(!I::Same(frame.owner,I::Capture(image,frame.owner.player,Read))){++identityRejected;return nullptr;}
    scope={};scope.device=device;scope.mask=mask;scope.index=index;std::memcpy(scope.plane,saved,sizeof(saved));scope.active=true;
    if(FAILED(device->SetClipPlane(index,plane))||FAILED(device->SetRenderState(D3DRS_CLIPPLANEENABLE,mask|(1u<<index)))){
        ++failed;scope.Restore();return scope.broken?&scope:nullptr;
    }
    ++clipped;return &scope;
}
HRESULT FinishDraw(void* token,HRESULT result){
    if(token!=&scope)return result;scope.Restore();const bool broken=scope.broken;scope={};return broken&&SUCCEEDED(result)?E_FAIL:result;
}
void InvalidateDevice(){scope.Restore();scope={};frame={};misses={};cache.Clear();}
Stats Snapshot(){return {installed.load(),frames.load(),carrying.load(),candidates.load(),clipped.load(),restored.load(),foreign.load(),identityRejected.load(),shaderRejected.load(),unsupported.load(),failed.load()};}
}
