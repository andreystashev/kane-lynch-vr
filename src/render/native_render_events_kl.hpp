#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace NativeRenderEventsKL {
// KL1's six auto-reset events are used exclusively by handle. KL2 creates
// DrawEnd with the same name as a manual-reset event in the same session.
inline constexpr std::uintptr_t Import=0x5321B4;
struct Site {
    std::uint32_t argument, returned, name;
    const char* literal;
};
inline constexpr Site Sites[]{
    {0xB7067,0xB7074,0x540D98,"DrawStart"},
    {0xB7074,0xB7087,0x540D8C,"DrawUpdate"},
    {0xB7087,0xB709A,0x540D84,"DrawEnd"},
    {0xB709A,0xB70AD,0x540D74,"DrawDebugBegin"},
    {0xB70AD,0xB70C0,0x540D64,"DrawDebugEnd"},
    {0xB70C0,0xB70D3,0x540D58,"DrawExit"}
};
template<class Read> bool Verify(std::uintptr_t image,Read read) {
    if(!image||image>UINT32_MAX-0x600000)return false;
    unsigned char load[6]{};
    const auto iat=static_cast<std::uint32_t>(image+Import);
    if(!read(image+0xB7061,load,sizeof(load))||load[0]!=0x8B||load[1]!=0x3D||
       std::memcmp(load+2,&iat,4))return false;
    for(const auto& site:Sites) {
        unsigned char start[11]{},call[2]{};
        char name[16]{};
        const auto expected=static_cast<std::uint32_t>(image+site.name);
        constexpr unsigned char zeros[]{0x6A,0,0x6A,0,0x6A,0};
        if(!read(image+site.argument,start,sizeof(start))||start[0]!=0x68||
           std::memcmp(start+1,&expected,4)||std::memcmp(start+5,zeros,sizeof(zeros))||
           !read(image+site.returned-2,call,sizeof(call))||call[0]!=0xFF||call[1]!=0xD7||
           !read(image+site.name,name,std::strlen(site.literal)+1)||
           std::memcmp(name,site.literal,std::strlen(site.literal)+1))return false;
    }
    return true;
}

inline bool IsOwned(std::uintptr_t image,std::uintptr_t caller,std::uintptr_t name) {
    for(const auto& site:Sites)
        if(caller==image+site.returned&&name==image+site.name)return true;
    return false;
}

inline std::atomic<decltype(&CreateEventA)> original{nullptr};
inline std::uintptr_t imageBase=0;
inline std::atomic<unsigned> created{0};
__declspec(noinline) inline HANDLE WINAPI Create(LPSECURITY_ATTRIBUTES security,BOOL manual,BOOL initial,LPCSTR name) {
    const auto call=original.load(std::memory_order_acquire);
    const bool local=!security&&!manual&&!initial&&IsOwned(imageBase,
        reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)),reinterpret_cast<std::uintptr_t>(name));
    const auto event=call(security,manual,initial,local?nullptr:name);
    if(local&&event)created.fetch_add(1,std::memory_order_relaxed);
    return event;
}

// Called on the suspended, independently qualified game before its entry
// point. Never replace a foreign import. The native engine owns all handles.
inline bool Install(){
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto read=[](std::uintptr_t at,void* out,std::size_t count){
        SIZE_T copied=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,count,&copied)&&copied==count;
    };
    if(!Verify(image,read))return false;
    auto** slot=reinterpret_cast<void**>(image+Import);
    void* expected=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateEventA"));
    if(!expected||*slot!=expected)return false;
    imageBase=image;
    original.store(reinterpret_cast<decltype(&CreateEventA)>(expected),std::memory_order_release);
    DWORD protection=0;
    if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection))return false;
    const bool installed=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&Create),expected)==expected;
    DWORD unused=0;
    const bool restored=VirtualProtect(slot,sizeof(*slot),protection,&unused)!=FALSE;
    if(installed&&!restored)InterlockedCompareExchangePointer(slot,expected,reinterpret_cast<void*>(&Create));
    return installed&&restored;
}
}
