#pragma once
#include "diagnostic_session.hpp"
#include "release_version.hpp"

// Startup failures precede the game/session monitor. Keep their exact route
// and native process result beside the package automatically, including when
// the launcher is hidden behind Settings. Diagnostic failure is fail-open.
namespace LauncherStartupTrace {
struct LastError {
    DWORD value=GetLastError();
    ~LastError(){SetLastError(value);}
};
class Run {
public:
    Run()=default;
    Run(const Run&)=delete;
    Run& operator=(const Run&)=delete;
    ~Run(){LastError preserve;if(file_!=INVALID_HANDLE_VALUE){FlushFileBuffers(file_);CloseHandle(file_);}}
    bool Begin(const std::wstring& package,const std::wstring& game,const char* hash) noexcept {
        LastError preserve;
        try {
            if(file_!=INVALID_HANDLE_VALUE||!DiagnosticsSession::Enabled(package))return false;
            const auto directory=DiagnosticsSession::Join(package,L"logs");
            if(!DiagnosticsSession::Ensure(directory))return false;
            SYSTEMTIME now{};GetSystemTime(&now);static LONG serial=0;wchar_t name[180]{};
            std::swprintf(name,180,L"launcher-%04u%02u%02uT%02u%02u%02u-%lu-%016llx-%ld.jsonl",
                now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),
                static_cast<unsigned long long>(DiagnosticsSession::Creation(GetCurrentProcess())),InterlockedIncrement(&serial));
            const auto path=DiagnosticsSession::Join(directory,name);
            file_=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file_==INVALID_HANDLE_VALUE)return false;
            const auto text="{\"event\":\"launcher_start\",\"formatVersion\":1,\"version\":\"" KL1_RELEASE_VERSION "\",\"utc\":"+
                DiagnosticsSession::Quote(DiagnosticsSession::Utc())+",\"launcherPid\":"+std::to_string(GetCurrentProcessId())+
                ",\"launcherCreationFt\":"+std::to_string(DiagnosticsSession::Creation(GetCurrentProcess()))+
                ",\"gameExe\":"+DiagnosticsSession::Quote(DiagnosticsSession::Utf8(game))+
                ",\"gameExeSha256\":"+DiagnosticsSession::Quote(hash?hash:"")+"}\n";
            Write(text);return true;
        }catch(...){return false;}
    }
    void Stage(const char* phase,const std::wstring& detail={},DWORD pid=0,DWORD windowsError=0,
               DWORD nativeExitCode=0,bool flag=false) noexcept {
        LastError preserve;
        try {
            if(file_==INVALID_HANDLE_VALUE)return;
            Write("{\"event\":\"launcher_stage\",\"utc\":"+DiagnosticsSession::Quote(DiagnosticsSession::Utc())+
                ",\"phase\":"+DiagnosticsSession::Quote(phase?phase:"")+
                ",\"detail\":"+DiagnosticsSession::Quote(DiagnosticsSession::Utf8(detail))+
                ",\"processId\":"+std::to_string(pid)+",\"windowsError\":"+std::to_string(windowsError)+
                ",\"nativeExitCode\":"+std::to_string(nativeExitCode)+",\"flag\":"+(flag?"true":"false")+"}\n");
        }catch(...){ }
    }
    int Result(int code) noexcept {
        Stage("launcher_result",{},0,0,static_cast<DWORD>(code));return code;
    }
private:
    HANDLE file_=INVALID_HANDLE_VALUE;
    void Write(const std::string& text) noexcept {
        if(file_==INVALID_HANDLE_VALUE||text.size()>32768)return;
        DWORD written=0;WriteFile(file_,text.data(),static_cast<DWORD>(text.size()),&written,nullptr);
    }
};
}
