#pragma once
#include <windows.h>
#pragma push_macro("WINVER")
#undef WINVER
#define WINVER 0x0602
#include <cfgmgr32.h>
#pragma pop_macro("WINVER")
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace NativeHidRefreshKL {
constexpr std::uintptr_t ContextRva=0x697888, DirectInputRva=0x6C14A0;
constexpr std::uintptr_t StartupCallRva=0x4C001, PollCallRva=0x4C0BF;
constexpr std::uintptr_t StartupReturnRva=0x4C011, StartupCallbackRva=0x4BB90;
constexpr std::uintptr_t PollReturnRva=0x4C0DA, PollCallbackRva=0x4BE80;
constexpr unsigned EnumSlot=4;
inline constexpr GUID HidClass{0x4D1E55B2,0xF16F,0x11CF,{0x88,0xCB,0,0x11,0x11,0,0,0x30}};
using Enum=HRESULT (WINAPI*)(IDirectInput8A*,DWORD,LPDIENUMDEVICESCALLBACKA,LPVOID,DWORD);
using Log=void (*)(const char*);

inline bool VerifiedCode(std::uintptr_t image) {
    // The startup call has a different callback and zero flags. The
    // periodic call only discovers pads, and ignores the HRESULT.
    const unsigned char startup[]{0x8B,0x10,0x57,0x56,0x68,0x90,0xBB,0x44,0,0x57,0x50,0x8B,0x42,0x10,0xFF,0xD0};
    const unsigned char poll[]{0xA1,0xA0,0x14,0xAC,0,0x85,0xC0,0x74,0x12,0x8B,0x08,0x8B,0x51,0x10,
        0x6A,1,0x56,0x68,0x80,0xBE,0x44,0,0x6A,0,0x50,0xFF,0xD2,0x83,0x3D,0xA4,0x14,0xAC,0,0};
    return image==0x400000&&std::memcmp(reinterpret_cast<const void*>(image+StartupCallRva),startup,sizeof(startup))==0&&
        std::memcmp(reinterpret_cast<const void*>(image+PollCallRva),poll,sizeof(poll))==0;
}
inline bool OwnedCall(std::uintptr_t image,std::uintptr_t ret,IDirectInput8A* instance,IDirectInput8A* active,
                      DWORD type,LPDIENUMDEVICESCALLBACKA cb,LPVOID parameter,LPVOID manager,DWORD flags) {
    return ret==image+PollReturnRva&&instance&&instance==active&&type==DI8DEVCLASS_ALL&&
        reinterpret_cast<std::uintptr_t>(cb)==image+PollCallbackRva&&parameter&&parameter==manager&&flags==DIEDFL_ATTACHEDONLY;
}

struct Refresh {
    std::atomic<unsigned> generation{1}, scanned{0}, scans{0}, skipped{0}, changes{0}, failures{0};
    std::atomic<void*> instance{nullptr}, manager{nullptr};
    void Invalidate() {generation.fetch_add(1,std::memory_order_acq_rel);}
    void Changed() {
        ++changes;
        Invalidate();
    }
    void ObserveInstance(void* input,void* owner) {
        const bool inputChanged=instance.exchange(input,std::memory_order_relaxed)!=input;
        const bool managerChanged=manager.exchange(owner,std::memory_order_relaxed)!=owner;
        if(inputChanged||managerChanged)Invalidate();
    }
    template<class Scan> HRESULT Poll(Scan native) {
        const auto request=generation.load(std::memory_order_acquire);
        if(scanned.load(std::memory_order_acquire)==request) {
            ++skipped;
            return DI_OK;
        }
        ++scans;
        const auto result=native();
        if(SUCCEEDED(result))scanned.store(request,std::memory_order_release);
        else ++failures;
        return result;
    }
};
inline Refresh refresh;
inline std::atomic<bool> enabled{false};
inline std::atomic<Enum> nativeEnum{nullptr};
inline std::atomic<unsigned> passthrough{0};
inline std::uintptr_t imageBase=0;
inline bool pending=false;
inline HMODULE cfg=nullptr;
inline HCMNOTIFICATION subscription=nullptr;
inline decltype(&CM_Unregister_Notification) unsubscribe=nullptr;
inline void** enumSlot=nullptr;

inline DWORD CALLBACK OnNotification(HCMNOTIFICATION,void*,CM_NOTIFY_ACTION action,CM_NOTIFY_EVENT_DATA*,DWORD) {
    if(action==CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL||action==CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)
        refresh.Changed();
    return ERROR_SUCCESS;
}
inline HRESULT WINAPI Intercept(IDirectInput8A* input,DWORD type,LPDIENUMDEVICESCALLBACKA callback,LPVOID parameter,DWORD flags) {
    const auto real=nativeEnum.load(std::memory_order_acquire);
    const auto scan=[&]{return real(input,type,callback,parameter,flags);};
    const auto ret=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(!enabled.load(std::memory_order_acquire)) {
        ++passthrough;
        return scan();
    }
    if(ret==imageBase+StartupReturnRva&&type==DI8DEVCLASS_ALL&&flags==0&&parameter&&
       reinterpret_cast<std::uintptr_t>(callback)==imageBase+StartupCallbackRva)refresh.Invalidate();
    if(ret!=imageBase+PollReturnRva) {
        ++passthrough;
        return scan();
    }
    const auto active=*reinterpret_cast<IDirectInput8A* const volatile*>(imageBase+DirectInputRva);
    const auto manager=*reinterpret_cast<void* const volatile*>(imageBase+ContextRva);
    if(!OwnedCall(imageBase,ret,input,active,type,callback,parameter,manager,flags)) {
        ++passthrough;
        return scan();
    }
    refresh.ObserveInstance(input,manager);
    return refresh.Poll(scan);
}

inline bool WritableSwap(void** slot,void* oldValue,void* replacement) {
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old))return false;
    const bool written=InterlockedCompareExchangePointer(slot,replacement,oldValue)==oldValue;
    DWORD ignored=0;
    VirtualProtect(slot,sizeof(*slot),old,&ignored);
    return written;
}
inline bool ReadBytes(const void* address,void* data,std::size_t size) {
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),address,data,size,&read)&&read==size;
}
inline bool SystemModule(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    const auto pathLength=GetModuleFileNameW(module,path,MAX_PATH);
    if(!pathLength||pathLength>=MAX_PATH)return false;
    // WOW64 may report the original System32 path even when the loader resolved it to SysWOW64.
    const auto matches=[&](auto directory) {
        wchar_t system[MAX_PATH]{};
        const auto length=directory(system,MAX_PATH);
        if(!length||length>=MAX_PATH-13)return false;
        std::wcscat(system,L"\\dinput8.dll");
        return _wcsicmp(system,path)==0;
    };
    return matches(GetSystemDirectoryW)||matches(GetSystemWow64DirectoryW);
}
inline void Prepare(std::uintptr_t image,bool useRefresh) {
    if(VerifiedCode(image)){imageBase=image;pending=useRefresh;}
}
inline bool WatchHid() {
    cfg=LoadLibraryExW(L"cfgmgr32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!cfg)return false;
    const auto subscribe=reinterpret_cast<decltype(&CM_Register_Notification)>(GetProcAddress(cfg,"CM_Register_Notification"));
    unsubscribe=reinterpret_cast<decltype(&CM_Unregister_Notification)>(GetProcAddress(cfg,"CM_Unregister_Notification"));
    if(!subscribe||!unsubscribe)return false;
    CM_NOTIFY_FILTER filter{};
    filter.cbSize=sizeof(filter);
    filter.FilterType=CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid=HidClass;
    return subscribe(&filter,nullptr,OnNotification,&subscription)==CR_SUCCESS;
}
inline void StopWatchingHid() {
    if(subscription&&unsubscribe)unsubscribe(subscription);
    subscription=nullptr;
    if(cfg)FreeLibrary(cfg);
    cfg=nullptr;
}
inline void Service(Log log) {
    if(!pending)return;
    IDirectInput8A* input=nullptr;
    if(!ReadBytes(reinterpret_cast<const void*>(imageBase+DirectInputRva),&input,sizeof(input))||!input)return;
    pending=false;
    void** table=nullptr;
    void* target=nullptr;
    MEMORY_BASIC_INFORMATION owner{}, code{};
    const auto library=GetModuleHandleW(L"dinput8.dll");
    if(!library||!SystemModule(library)||!ReadBytes(input,&table,sizeof(table))||!table||
       !VirtualQuery(table,&owner,sizeof(owner))||owner.AllocationBase!=library||
       !ReadBytes(table+EnumSlot,&target,sizeof(target))||!target||
       !VirtualQuery(target,&code,sizeof(code))||code.AllocationBase!=library||
       code.State!=MEM_COMMIT||!(code.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) {
        if(log)log("HID discovery adapter unavailable for this DirectInput implementation.");
        return;
    }
    if(!WatchHid()) {
        StopWatchingHid();
        if(log)log("HID device-change registration failed. Using native controller discovery.");
        return;
    }
    HMODULE lifetime=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&Intercept),&lifetime)) {
        StopWatchingHid();
        return;
    }
    nativeEnum.store(reinterpret_cast<Enum>(target),std::memory_order_release);
    if(!WritableSwap(table+EnumSlot,target,reinterpret_cast<void*>(&Intercept))) {
        StopWatchingHid();
        if(log)log("DirectInput slot changed. Using native controller discovery.");
        return;
    }
    enumSlot=table+EnumSlot;
    enabled.store(true,std::memory_order_release);
    if(log)log("Controller discovery refreshes on HID topology changes.");
}
inline void Shutdown() {
    enabled.store(false,std::memory_order_release);
    if(enumSlot)
        WritableSwap(enumSlot,reinterpret_cast<void*>(&Intercept),reinterpret_cast<void*>(nativeEnum.load(std::memory_order_acquire)));
    enumSlot=nullptr;
    StopWatchingHid();
}
}
