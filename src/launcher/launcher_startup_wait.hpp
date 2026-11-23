#pragma once
#include "diagnostic_session.hpp"
#include "profile.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <vector>

// Wait for the selected game, not merely a living official bootstrap. This
// helper never injects, resumes, terminates, or changes either executable.
namespace LauncherStartupWait {
inline bool SamePath(std::wstring a,std::wstring b){
    const auto normalize=[](std::wstring& path){for(auto& c:path)if(c==L'/')c=L'\\';
        if(path.compare(0,4,L"\\\\?\\")==0)path.erase(0,4);};
    normalize(a);normalize(b);return _wcsicmp(a.c_str(),b.c_str())==0;
}
inline bool MatchesSelected(HANDLE process,const std::wstring& selected,const char* expectedHash,
                            std::uint64_t minimumCreation,std::uint64_t exactCreation=0){
    if(!process||!minimumCreation||!expectedHash||std::strlen(expectedHash)!=64||
       WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return false;
    const auto created=DiagnosticsSession::Creation(process);
    if(!created||created<minimumCreation||(exactCreation&&created!=exactCreation))return false;
    wchar_t actual[32768]{};DWORD length=32768;char digest[65]{};
    return QueryFullProcessImageNameW(process,0,actual,&length)&&SamePath(actual,selected)&&
        HashFile(actual,digest)&&!std::strcmp(digest,expectedHash)&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;
}
enum class Outcome {Ready,NativeExited,TimedOut,EnumerationFailed,Invalid};
struct Options {
    HANDLE initialProcess=nullptr;DWORD initialPid=0;
    std::uint64_t initialCreation=0;
    std::wstring selected;std::string expectedHash;
    std::vector<DWORD> before;
    bool initialMayBeGame=false;
    DWORD timeoutMs=20000,pollMs=50,initialGameGraceMs=1500,exitGraceMs=750;
};
struct Result {
    Outcome outcome=Outcome::Invalid;
    // A Ready result owns a new process handle, even for the initial PID.
    // The caller closes it; the supplied initial handle is always borrowed.
    HANDLE process=nullptr;DWORD pid=0;std::uint64_t creation=0;
    DWORD error=0,nativeExitCode=STILL_ACTIVE;bool rebound=false;
    unsigned rejected=0,polls=0;
};
inline Result Wait(const Options& options){
    Result result;
    const auto slash=options.selected.find_last_of(L"\\/");
    const auto name=options.selected.substr(slash==std::wstring::npos?0:slash+1);
    if(!options.initialProcess||!options.initialPid||!options.initialCreation||name.empty()||
       options.expectedHash.size()!=64||!options.timeoutMs||options.timeoutMs>60000||
       !options.pollMs||options.pollMs>1000||options.exitGraceMs>options.timeoutMs){result.error=ERROR_INVALID_PARAMETER;return result;}
    const DWORD started=GetTickCount();DWORD exitObserved=0;bool exited=false;
    for(;;){
        ++result.polls;const DWORD now=GetTickCount();
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE){result.outcome=Outcome::EnumerationFailed;result.error=GetLastError();return result;}
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
        const auto candidate=[&](DWORD pid){
            if(!pid)return;
            for(const auto old:options.before)if(pid==old)return;
            if(pid==options.initialPid&&(!options.initialMayBeGame||now-started<options.initialGameGraceMs))return;
            HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|
                PROCESS_VM_WRITE|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid);
            if(!process){++result.rejected;return;}
            const auto floor=options.initialCreation+(pid==options.initialPid?0u:1u);
            if(!MatchesSelected(process,options.selected,options.expectedHash.c_str(),floor)){
                ++result.rejected;CloseHandle(process);return;}
            result.outcome=Outcome::Ready;result.process=process;result.pid=pid;
            result.creation=DiagnosticsSession::Creation(process);result.rebound=pid!=options.initialPid;
        };
        bool enumerated=Process32FirstW(snapshot,&entry)!=FALSE;DWORD enumerationError=enumerated?0:GetLastError();
        if(enumerated)do {
            if(!_wcsicmp(entry.szExeFile,name.c_str()))candidate(entry.th32ProcessID);
            if(result.process)break;
        }while(Process32NextW(snapshot,&entry));
        if(enumerated&&!result.process)enumerationError=GetLastError();
        CloseHandle(snapshot);
        if(result.process)return result;
        if(enumerationError&&enumerationError!=ERROR_NO_MORE_FILES){result.outcome=Outcome::EnumerationFailed;result.error=enumerationError;return result;}
        const auto wait=WaitForSingleObject(options.initialProcess,0);
        if(wait==WAIT_OBJECT_0){
            if(!exited){exited=true;exitObserved=GetTickCount();
                if(!GetExitCodeProcess(options.initialProcess,&result.nativeExitCode))result.error=GetLastError();}
            if(GetTickCount()-exitObserved>=options.exitGraceMs){result.outcome=Outcome::NativeExited;return result;}
        }else if(wait!=WAIT_TIMEOUT){result.outcome=Outcome::Invalid;result.error=GetLastError();return result;}
        const DWORD elapsed=GetTickCount()-started;
        if(elapsed>=options.timeoutMs){result.outcome=Outcome::TimedOut;return result;}
        Sleep(std::min(options.pollMs,options.timeoutMs-elapsed));
    }
}
}
