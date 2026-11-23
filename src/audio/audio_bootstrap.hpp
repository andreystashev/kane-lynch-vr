#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <string>

// Private x86 early-start contract. Per-application playback routing only;
// no system-wide default endpoint, volume or capture-device changes.
namespace AudioBootstrap {
constexpr std::uint32_t Magic=0x4b4c4155,Version=1;
constexpr const char* OfficialLauncherSha="139961bd6a920f8ec5162eadd3e91a88a197721d421b7dcabca06bc8ab153b79";
constexpr const char* OfficialSteamLauncherSha="827c654a9c54766a9b92f397663917f97ad86ce56a8ca9d0a636d9cd2dfad248";
constexpr const char* OfficialUnwrappedLauncherSha="bbef9a979b04f8d7fcdcd6e970b92a0877c9cdc54db7317eb1b3d69e6b3dd580";
inline bool IsSupportedOfficialLauncherHash(const char* digest) {
    return digest&&(!std::strcmp(digest,OfficialLauncherSha)||!std::strcmp(digest,OfficialSteamLauncherSha)||!std::strcmp(digest,OfficialUnwrappedLauncherSha));
}
struct Request {
    std::uint32_t magic=Magic,version=Version,entryRva=0,fallback=1;
    GUID endpoint{};
    wchar_t gamePath[MAX_PATH]{},modulePath[MAX_PATH]{};
};
static_assert(sizeof(Request)==1072,"Early audio request ABI");
inline DWORD EntryRva(const wchar_t* path) {
    HMODULE module=LoadLibraryExW(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!module)return 0;
    auto entry=GetProcAddress(module,"KaneLynchAudioInitialize@4");
    if(!entry)entry=GetProcAddress(module,"KaneLynchAudioInitialize");
    const DWORD offset=entry?static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(entry)-reinterpret_cast<std::uintptr_t>(module)):0;
    FreeLibrary(module);return offset;
}
inline bool Invoke(HANDLE process,LPTHREAD_START_ROUTINE routine,void* argument,DWORD& result) {
    HANDLE thread=CreateRemoteThread(process,nullptr,0,routine,argument,0,nullptr);if(!thread)return false;
    const DWORD wait=WaitForSingleObject(thread,10000);result=0;
    const bool okay=wait==WAIT_OBJECT_0&&GetExitCodeThread(thread,&result);
    CloseHandle(thread);return okay;
}
inline bool Attach(HANDLE process,const Request& request) {
    if(!request.entryRva||!request.modulePath[0])return false;
    const SIZE_T pathBytes=(wcslen(request.modulePath)+1)*sizeof(wchar_t);
    void* path=VirtualAllocEx(process,nullptr,pathBytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!path)return false;
    SIZE_T copied=0;DWORD module=0;
    const auto load=reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"));
    if(!load||!WriteProcessMemory(process,path,request.modulePath,pathBytes,&copied)||copied!=pathBytes) {
        VirtualFreeEx(process,path,0,MEM_RELEASE);return false;
    }
    if(!Invoke(process,load,path,module))return false; // timeout may still use path
    VirtualFreeEx(process,path,0,MEM_RELEASE);
    if(!module)return false;
    void* remote=VirtualAllocEx(process,nullptr,sizeof(request),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!remote)return false;
    if(!WriteProcessMemory(process,remote,&request,sizeof(request),&copied)||copied!=sizeof(request)) {
        VirtualFreeEx(process,remote,0,MEM_RELEASE);return false;
    }
    DWORD installed=0;
    const auto entry=reinterpret_cast<LPTHREAD_START_ROUTINE>(static_cast<std::uintptr_t>(module)+request.entryRva);
    if(!Invoke(process,entry,remote,installed))return false; // timeout may still read request
    VirtualFreeEx(process,remote,0,MEM_RELEASE);return installed==1;
}
}
