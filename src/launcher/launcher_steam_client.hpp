#pragma once
#include <windows.h>
#include <string>
#include <cwchar>
#include <cstdio>

// Start the already-installed Steam client, not the game through a second
// URL/launch path. The existing official launcher still owns game startup.
namespace LauncherSteamClient {
inline bool Ready(DWORD pid,bool identity,bool alive){return pid&&identity&&alive;}
class Registry {
    HMODULE module_=LoadLibraryExW(L"advapi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    using GetValue=LSTATUS(WINAPI*)(HKEY,LPCWSTR,LPCWSTR,DWORD,LPDWORD,PVOID,LPDWORD);
    GetValue get_=module_?reinterpret_cast<GetValue>(GetProcAddress(module_,"RegGetValueW")):nullptr;
public:
    ~Registry(){if(module_)FreeLibrary(module_);}
    bool Dword(const wchar_t* key,const wchar_t* name,DWORD& out){
        out=0;DWORD bytes=sizeof(out);return get_&&get_(HKEY_CURRENT_USER,key,name,RRF_RT_REG_DWORD,nullptr,&out,&bytes)==ERROR_SUCCESS&&bytes==sizeof(out);
    }
    bool Text(const wchar_t* key,const wchar_t* name,std::wstring& out){
        wchar_t value[32768]{};DWORD bytes=sizeof(value);
        if(!get_||get_(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ,nullptr,value,&bytes)!=ERROR_SUCCESS||bytes<sizeof(wchar_t)||bytes>sizeof(value)||value[bytes/sizeof(wchar_t)-1])return false;
        out=value;for(auto& c:out)if(c==L'/')c=L'\\';return !out.empty();
    }
};
inline bool ClientReady(Registry& registry,const std::wstring& exe){
    DWORD pid=0;
    if(!registry.Dword(L"Software\\Valve\\Steam\\ActiveProcess",L"pid",pid))return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return false;
    wchar_t actual[32768]{};DWORD length=32768;
    const bool identity=QueryFullProcessImageNameW(process,0,actual,&length)&&_wcsicmp(actual,exe.c_str())==0;
    const bool alive=WaitForSingleObject(process,0)==WAIT_TIMEOUT;CloseHandle(process);
    // ActiveUser can remain zero while the unchanged original bootstrap
    // successfully starts this owned game. It is not an authentication API.
    // The official Steam launcher retains its subscription/login authority.
    return Ready(pid,identity,alive);
}
inline bool Ensure(std::wstring& error){
    Registry registry;std::wstring exe;
    if(!registry.Text(L"Software\\Valve\\Steam",L"SteamExe",exe)){
        error=L"Steam installation was not found. Open the installed Steam client once and try again.";return false;
    }
    const auto slash=exe.find_last_of(L'\\');
    const auto attributes=GetFileAttributesW(exe.c_str());
    if(slash==std::wstring::npos||_wcsicmp(exe.c_str()+slash+1,L"steam.exe")||attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)){
        error=L"The registered Steam client path is invalid. Start Steam once and try again.";return false;
    }
    if(ClientReady(registry,exe))return true;
    std::puts("Starting Steam and waiting for the client process before native game startup...");
    auto command=L"\""+exe+L"\" -silent";const auto folder=exe.substr(0,slash);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,folder.c_str(),&startup,&process)){
        error=L"Could not start the installed Steam client (Windows error "+std::to_wstring(GetLastError())+L").";return false;
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    const auto start=GetTickCount64();
    do{if(ClientReady(registry,exe)){Sleep(1000);if(ClientReady(registry,exe))return true;}Sleep(250);}while(GetTickCount64()-start<60000);
    error=L"The installed Steam client did not become available within 60 seconds. Open Steam, let its startup finish, then press Save & play again. No game or VR DLL was started.";
    return false;
}
}
