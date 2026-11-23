#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "native_skin_lighting_kl.hpp"
#include "native_skin_lighting_shader.hpp"
#include "native_hands_kl.hpp"
#include <atomic>
#include <vector>
#include <cstring>
namespace NativeSkinLightingKL {
namespace {
std::atomic<std::uint64_t> candidates{0},matched{0},draws{0},restored{0},failed{0},cacheHits{0},cacheMisses{0};
struct Cached {IDirect3DVertexShader9* source=nullptr;IDirect3DVertexShader9* replacement=nullptr;};
thread_local std::vector<Cached> cache;
thread_local IDirect3DDevice9* ownerDevice=nullptr;
struct Scope {bool active=false;IDirect3DDevice9* device=nullptr;IDirect3DVertexShader9* source=nullptr,*replacement=nullptr;};
thread_local Scope scope;
std::uint32_t Hash(const void* bytes,std::size_t count){const auto* p=static_cast<const unsigned char*>(bytes);std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<count;++i){h^=p[i];h*=16777619u;}return h;}
IDirect3DVertexShader9* Replacement(IDirect3DDevice9* device,IDirect3DVertexShader9* source) {
    for(const auto& entry:cache)if(entry.source==source){++cacheHits;return entry.replacement;}
    ++cacheMisses;Cached next;next.source=source;UINT bytes=0;
    if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&!(bytes%4)) {
        std::vector<std::uint32_t> code(bytes/4);
        if(SUCCEEDED(source->GetFunction(code.data(),&bytes))) {
            const auto hash=Hash(code.data(),bytes);
            for(const auto& known:NativeSkinLightingShader::Entries)if(hash==known.originalHash&&bytes==known.originalBytes&&
                !std::memcmp(code.data(),known.original,bytes)) {
                ++matched;if(FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(known.patched),&next.replacement)))++failed;break;
            }
        }
    }
    if(cache.size()>=64){auto& old=cache.front();if(old.replacement)old.replacement->Release();old.source->Release();cache.erase(cache.begin());}
    source->AddRef();cache.push_back(next);return next.replacement;
}
void Restore() {
    if(!scope.active)return;const auto old=scope;scope={};IDirect3DVertexShader9* current=nullptr;
    if(SUCCEEDED(old.device->GetVertexShader(&current))) {
        if(current==old.replacement){if(SUCCEEDED(old.device->SetVertexShader(old.source)))++restored;else ++failed;}
        else ++failed;
        if(current)current->Release();
    } else ++failed;
    old.source->Release();
}
void ClearCache(){for(auto& entry:cache){if(entry.replacement)entry.replacement->Release();entry.source->Release();}cache.clear();}
}
void InvalidateDevice(){Restore();ClearCache();ownerDevice=nullptr;}
void* BeginDraw(IDirect3DDevice9* device) {
    if(!device||scope.active||!NativeHandsKL::CoupledSkinLightingActive())return nullptr;
    ++candidates;
    if(ownerDevice!=device){ClearCache();ownerDevice=device;}
    IDirect3DVertexShader9* source=nullptr;
    if(FAILED(device->GetVertexShader(&source))||!source)return nullptr;
    auto* replacement=Replacement(device,source);
    if(!replacement){source->Release();return nullptr;}
    if(FAILED(device->SetVertexShader(replacement))){source->Release();++failed;return nullptr;}
    scope={true,device,source,replacement};++draws;return &scope;
}
HRESULT FinishDraw(void* token,HRESULT result){if(token==&scope)Restore();return result;}
Stats Snapshot(){return {candidates.load(),matched.load(),draws.load(),restored.load(),failed.load(),cacheHits.load(),cacheMisses.load()};}
}
