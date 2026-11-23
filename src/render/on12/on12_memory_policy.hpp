#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstring>

// Process-local D3D9On12 compatibility policy. The seven-DWORD interface is
// defined in Microsoft's MIT-licensed interface/d3d9on12ddi.h. No registry
// settings, Microsoft DLL files or game executable bytes are changed.
namespace On12Memory {
struct Compat {
    DWORD shaderMask, psoSize, psoAge, uploadBytes, srvSize, trimFences, disableIntzFix;
};
static_assert(sizeof(Compat)==28,"D3D9ON12_APP_COMPAT_INFO ABI");
constexpr DWORD UploadLimit=8*1024*1024, TrimLimit=5;
inline Compat Clamp(Compat value) {
    if(value.uploadBytes>UploadLimit)value.uploadBytes=UploadLimit;
    if(value.trimFences>TrimLimit)value.trimFences=TrimLimit;
    return value;
}
using Setter=void(WINAPI*)(const Compat*);
using Logger=void(*)(const char*,const Compat*);
inline Setter original=nullptr;
inline void* target=nullptr;
inline Compat* settings=nullptr;
inline Logger logger=nullptr;
inline LONG updates=0;
inline void WINAPI Filter(const Compat* incoming) {
    const DWORD error=GetLastError();
    const auto bounded=Clamp(*incoming);
    original(&bounded);
    if(logger&&InterlockedIncrement(&updates)<=8)logger("applied",&bounded);
    SetLastError(error);
}
// Accept only the complete known x86 seven-DWORD copy setter. Unknown OS
// implementations keep their normal behavior instead of guessing an ABI.
inline Compat* Validate(HMODULE module,void* address) {
    if(!module||!address)return nullptr;
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return nullptr;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return nullptr;
    const auto end=base+nt->OptionalHeader.SizeOfImage;
    const auto code=reinterpret_cast<std::uintptr_t>(address);
    if(code<base||code>end-26)return nullptr;
    constexpr unsigned char expected[]={0x8b,0xff,0x55,0x8b,0xec,0x56,0x8b,0x75,0x08,0x57,0x6a,0x07,0x59,0xbf,0,0,0,0,0xf3,0xa5,0x5f,0x5e,0x5d,0xc2,0x04,0};
    auto* bytes=static_cast<const unsigned char*>(address);
    for(unsigned i=0;i<sizeof(expected);++i)if((i<14||i>17)&&bytes[i]!=expected[i])return nullptr;
    std::uint32_t data=0;std::memcpy(&data,bytes+14,sizeof(data));
    if(data<base||data>end-sizeof(Compat))return nullptr;
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<void*>(data),&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
       !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||
       data+sizeof(Compat)>reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize)return nullptr;
    return reinterpret_cast<Compat*>(data);
}
inline bool Install(Logger log=nullptr) {
    logger=log;
    // Called once by the selector factory, outside the loader lock and before
    // D3D9 creates its D3D12 translation device. Retain the module for the hook.
    const auto module=LoadLibraryExW(L"d3d9on12.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    target=module?reinterpret_cast<void*>(GetProcAddress(module,"SetAppCompatData")):nullptr;
    settings=Validate(module,target);
    if(!settings){if(logger)logger("unsupported_abi",nullptr);return false;}
    const auto init=MH_Initialize();
    if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){if(logger)logger("hook_init_failed",nullptr);return false;}
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Filter),reinterpret_cast<void**>(&original))!=MH_OK){
        if(logger)logger("hook_create_failed",nullptr);return false;
    }
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);original=nullptr;if(logger)logger("hook_enable_failed",nullptr);return false;}
    // Seed current defaults too; later native compatibility updates pass
    // through Filter, preserving every unrelated setting and stricter limits.
    const Compat current=*settings;Filter(&current);
    return true;
}
}
