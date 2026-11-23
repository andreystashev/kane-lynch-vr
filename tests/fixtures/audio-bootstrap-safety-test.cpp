// Exercises early LoadLibrary/Initialize without a game, audio stream, or
// settings writes. The supported-image guard must reject this executable.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include "audio_bootstrap.hpp"
int wmain(int argc,wchar_t** argv) {
    if(argc==2&&!std::wcscmp(argv[1],L"--child")) {
        const bool loaded=GetModuleHandleW(L"KaneLynchVRAudio.dll")!=nullptr;
        const bool native=reinterpret_cast<std::uintptr_t>(&CreateProcessW)==reinterpret_cast<std::uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateProcessW"));
        return loaded&&native?0:11;
    }
    if(argc!=2){std::puts("usage: audio-bootstrap-safety-test.exe full-path-to-KaneLynchVRAudio.dll");return 1;}
    wchar_t module[MAX_PATH]{},self[MAX_PATH]{};
    if(!GetFullPathNameW(argv[1],MAX_PATH,module,nullptr)||!GetModuleFileNameW(nullptr,self,MAX_PATH))return 2;
    AudioBootstrap::Request request;std::wcscpy(request.modulePath,module);std::wcscpy(request.gamePath,self);
    request.endpoint.Data1=1;request.entryRva=AudioBootstrap::EntryRva(module);if(!request.entryRva)return 3;
    std::wstring command=L"\""+std::wstring(self)+L"\" --child";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&child))return 4;
    const bool applied=AudioBootstrap::Attach(child.hProcess,request);
    const bool resumed=ResumeThread(child.hThread)!=static_cast<DWORD>(-1);CloseHandle(child.hThread);
    const DWORD waited=resumed?WaitForSingleObject(child.hProcess,10000):WAIT_FAILED;DWORD code=99;
    if(waited==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&code);else TerminateProcess(child.hProcess,5);
    CloseHandle(child.hProcess);
    if(applied||!resumed||waited!=WAIT_OBJECT_0||code){std::printf("FAIL: applied=%u resumed=%u wait=%lu childExit=%lu\n",applied,resumed,waited,code);return 6;}
    std::puts("Audio bootstrap safety: early DLL load completed before entry; unsupported image rejected; native CreateProcess import preserved. No game, audio stream, or settings opened.");return 0;
}
