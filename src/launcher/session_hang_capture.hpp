#pragma once
#include "diagnostic_session.hpp"
#include <dbghelp.h>

// Runs only in the external observer. No target memory writes, termination,
// injected worker, render-thread dump I/O, or full-memory dump is used.
namespace SessionHangCapture {
inline bool Enabled(const std::wstring& package){
    return DiagnosticsSession::Enabled(package)&&GetPrivateProfileIntW(L"Diagnostics",L"HangCapture",0,
        DiagnosticsSession::Join(package,L"vr.ini").c_str())!=0;
}
inline constexpr MINIDUMP_TYPE DumpType=static_cast<MINIDUMP_TYPE>(
    MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithFullMemoryInfo);
inline bool Matches(HANDLE process,DWORD pid,std::uint64_t creation,const std::wstring& expected){
    wchar_t path[32768]{};DWORD count=32768;
    return process&&pid&&GetProcessId(process)==pid&&creation&&DiagnosticsSession::Creation(process)==creation&&
        WaitForSingleObject(process,0)==WAIT_TIMEOUT&&QueryFullProcessImageNameW(process,0,path,&count)&&
        !_wcsicmp(path,expected.c_str());
}
class Detector {
    bool counterKnown_=false,armed_=false,pending_=false,pendingCounterKnown_=false,attempted_=false;
    std::uint64_t previous_=0,since_=0,pendingPresents_=0;
public:
    static constexpr std::uint64_t SustainedMs=10000;
    bool Observe(std::uint64_t now,bool enabled,bool qualifiedCounter,std::uint64_t presents,
                 bool sourceFocused,bool windowKnown,bool windowHung,bool foreground,bool stale){
        if(attempted_)return false;
        if(!enabled){counterKnown_=armed_=pending_=false;return false;}
        // A startup wait, absent/foreign status, or a never-rendering process
        // cannot arm a dump. Require real progress after hooks were ready.
        if(qualifiedCounter&&sourceFocused&&foreground&&presents){
            if(counterKnown_&&presents>previous_)armed_=true;
            if(counterKnown_&&presents<previous_)armed_=false;
            previous_=presents;counterKnown_=true;
        }
        if(!armed_||!windowKnown||!windowHung){pending_=false;return false;}
        if(!pending_||now<since_){
            if(!foreground||!stale){pending_=false;return false;}
            pending_=true;since_=now;
            pendingCounterKnown_=qualifiedCounter;pendingPresents_=presents;
            return false;
        }
        // Windows may give focus to the ghost window after detecting an
        // already observed hang. Continue only with the same fresh counter
        // and an unresponsive window belonging to this exact process.
        const bool sameCounter=pendingCounterKnown_&&qualifiedCounter&&presents==pendingPresents_;
        if((pendingCounterKnown_&&qualifiedCounter&&!sameCounter)||
           ((!foreground||!stale)&&!sameCounter)){pending_=false;return false;}
        if(now-since_<SustainedMs)return false;
        attempted_=true;return true;
    }
    bool Armed()const{return armed_;}
    bool Attempted()const{return attempted_;}
};
struct Result {bool written=false;DWORD error=ERROR_SUCCESS;std::uint64_t bytes=0;};
inline Result Capture(const std::wstring& package,DWORD pid,std::uint64_t creation,const std::wstring& expected){
    Result out;
    if(!Enabled(package)){out.error=ERROR_CANCELLED;return out;}
    // Reopen with read-only dump rights only after the gate. PID lifetime and
    // the complete executable path are requalified on this retained handle.
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    if(!process){out.error=GetLastError();return out;}
    struct Close {HANDLE value;~Close(){CloseHandle(value);}} closeProcess{process};
    if(!Matches(process,pid,creation,expected)){out.error=ERROR_INVALID_PARAMETER;return out;}
    const auto directory=DiagnosticsSession::Directory(package,pid,creation);
    if(!DiagnosticsSession::Ensure(DiagnosticsSession::Join(package,L"logs"))||!DiagnosticsSession::Ensure(directory)){
        out.error=GetLastError();if(!out.error)out.error=ERROR_ACCESS_DENIED;return out;
    }
    HMODULE library=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library){out.error=GetLastError();return out;}
    struct Unload {HMODULE value;~Unload(){FreeLibrary(value);}} unload{library};
    using WriteDump=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
        PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
    const auto writeDump=reinterpret_cast<WriteDump>(GetProcAddress(library,"MiniDumpWriteDump"));
    if(!writeDump){out.error=GetLastError();return out;}
    // CREATE_NEW preserves any previous capture and any colliding personal
    // file. The directory is derived only from the qualified exact identity.
    HANDLE file=CreateFileW(DiagnosticsSession::Join(directory,L"hang.dmp").c_str(),GENERIC_WRITE,
        FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){out.error=GetLastError();return out;}
    Close closeFile{file};
    out.written=writeDump(process,pid,file,DumpType,nullptr,nullptr,nullptr)!=FALSE;
    out.error=out.written?ERROR_SUCCESS:GetLastError();
    if(!FlushFileBuffers(file)&&out.written){out.written=false;out.error=GetLastError();}
    LARGE_INTEGER size{};if(GetFileSizeEx(file,&size)&&size.QuadPart>=0)out.bytes=std::uint64_t(size.QuadPart);
    return out;
}
}
