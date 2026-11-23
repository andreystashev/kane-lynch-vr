#pragma once
#include "diagnostic_session.hpp"
#include "performance_log.hpp"
#include "profile.hpp"
#include "session_hang_capture.hpp"
#include <psapi.h>
#include <string>

namespace SessionMonitor {
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
inline bool Matches(HANDLE process,const std::wstring& image,std::uint64_t creation){
    wchar_t path[32768]{};DWORD size=32768;
    return creation&&DiagnosticsSession::Creation(process)==creation&&QueryFullProcessImageNameW(process,0,path,&size)&&
        !_wcsicmp(image.c_str(),path);
}
inline const char* ExitKind(DWORD code,bool exceptionRecord){
    if(exceptionRecord)return "recorded_exception";
    if(!code)return "zero_exit_without_recorded_exception";
    if(code>=0x80000000u)return "exception_or_forced_exit";
    return "nonzero_exit";
}
inline bool Write(const std::wstring& path,const std::string& bytes){
    const auto temporary=path+L".tmp-"+std::to_wstring(GetCurrentProcessId());
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD count=0;const bool okay=WriteFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(okay&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
    DeleteFileW(temporary.c_str());return false;
}
inline std::string Read(const std::wstring& path){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER size{};DWORD count=0;std::string bytes;
    // A final summary can embed both the full game and host snapshots, plus
    // metadata. Keep it bounded while accepting the expanded trace payloads.
    constexpr std::uint64_t maxSummaryBytes=2*PerformanceLog::MaxSnapshotBytes+65536;
    if(GetFileSizeEx(file,&size)&&size.QuadPart>0&&std::uint64_t(size.QuadPart)<=maxSummaryBytes){bytes.resize(std::size_t(size.QuadPart));
        if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size())bytes.clear();}
    CloseHandle(file);return bytes;
}
class Journal {
    HANDLE file=INVALID_HANDLE_VALUE;std::wstring path;std::uint64_t bytes=0;
public:
    ~Journal(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    bool Open(const std::wstring& session){path=DiagnosticsSession::Join(session,L"session.jsonl");
        file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        return file!=INVALID_HANDLE_VALUE;}
    bool Add(const std::string& object,bool durable=false){
        if(file==INVALID_HANDLE_VALUE)return false;
        if(bytes>2*1024*1024){CloseHandle(file);file=INVALID_HANDLE_VALUE;
            if(!MoveFileExW(path.c_str(),(path+L".1").c_str(),MOVEFILE_REPLACE_EXISTING))return false;
            file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);bytes=0;
            if(file==INVALID_HANDLE_VALUE)return false;}
        const auto line=object+"\n";DWORD written=0;
        if(!WriteFile(file,line.data(),DWORD(line.size()),&written,nullptr)||written!=line.size())return false;
        bytes+=written;return !durable||FlushFileBuffers(file);
    }
};
struct WindowSearch {DWORD pid=0;bool known=false,hung=false;};
inline BOOL CALLBACK FindWindow(HWND window,LPARAM parameter){
    auto& state=*reinterpret_cast<WindowSearch*>(parameter);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==state.pid&&IsWindowVisible(window)&&GetWindow(window,GW_OWNER)==nullptr){state.known=true;state.hung=IsHungAppWindow(window)!=FALSE;return FALSE;}
    return TRUE;
}
inline int Observe(HANDLE game,DWORD pid,std::uint64_t created,const std::wstring& expected,
                   const std::wstring& package,const char* version){
    if(!Matches(game,expected,created))return 3;
    wchar_t name[144]{};std::swprintf(name,144,L"Local\\KaneLynchVR-Observer-%lu-%016llx",pid,static_cast<unsigned long long>(created));
    Handle owner;owner.value=CreateMutexW(nullptr,TRUE,name);
    if(!owner.value)return 4;if(GetLastError()==ERROR_ALREADY_EXISTS)return 0;
    const auto directory=DiagnosticsSession::Directory(package,pid,created);
    if(!DiagnosticsSession::Ensure(DiagnosticsSession::Join(package,L"logs"))||!DiagnosticsSession::Ensure(directory))return 5;
    Journal journal;if(!journal.Open(directory))return 6;
    using DiagnosticsSession::Quote;using DiagnosticsSession::Utf8;
    const auto start=GetTickCount64();char exeHash[65]{},dllHash[65]{};
    HashFile(expected.c_str(),exeHash);HashFile(DiagnosticsSession::Join(package,L"KaneLynchVR.dll").c_str(),dllHash);
    const auto identity="\"gamePid\":"+std::to_string(pid)+",\"processCreationFileTime\":"+std::to_string(created);
    const auto manifest="{\"event\":\"session\",\"formatVersion\":1,\"utc\":"+Quote(DiagnosticsSession::Utc())+","+identity+
        ",\"monitorPid\":"+std::to_string(GetCurrentProcessId())+",\"version\":"+Quote(version)+",\"gameExe\":"+Quote(Utf8(expected))+
        ",\"gameExeSha256\":"+Quote(exeHash)+",\"modSha256\":"+Quote(dllHash)+",\"package\":"+Quote(Utf8(package))+"}";
    journal.Add(manifest,true);Write(DiagnosticsSession::Join(directory,L"manifest.json"),manifest+"\n");
    std::string lastGame="null",lastHost="null";std::uint64_t previousPresents=0,lastProgress=start,missingSince=0,maxMissing=0,stalledPresents=0;
    bool wasStale=false,wasHung=false,seenGame=false,counterSeen=false,previousFocused=false;unsigned observations=0,renderStalls=0;std::uint64_t lastSnapshot=0;
    SessionHangCapture::Detector hangCapture;
    Handle hostProcess;DWORD observedHost=0;std::uint64_t observedHostCreation=0;DWORD waitResult=WAIT_TIMEOUT;
    for(;;){
        const auto now=GetTickCount64();const auto tick=GetTickCount();
        auto sourceRecord=PerformanceLog::ReadSnapshot(DiagnosticsSession::Join(package,L"KaneLynchVR-status.json"),"pid",pid,tick,true);
        if(sourceRecord.Valid()&&PerformanceLog::FileTicks(sourceRecord.written)<created)sourceRecord.json="null";
        const auto& source=sourceRecord.json;
        const auto hostRecord=PerformanceLog::ReadHostSnapshot(package,pid,tick);const auto& host=hostRecord.json;
        if(source!="null"){lastGame=source;missingSince=0;seenGame=true;}else{if(!missingSince)missingSince=now;maxMissing=(std::max)(maxMissing,now-missingSince);}
        if(host!="null")lastHost=host;
        if(hostProcess.value&&WaitForSingleObject(hostProcess.value,0)==WAIT_OBJECT_0){
            DWORD code=0;GetExitCodeProcess(hostProcess.value,&code);char codeHex[16]{};std::snprintf(codeHex,sizeof(codeHex),"0x%08lx",static_cast<unsigned long>(code));
            journal.Add("{\"event\":\"host_exit\",\"utc\":"+Quote(DiagnosticsSession::Utc())+",\"hostPid\":"+std::to_string(observedHost)+
                ",\"processCreationFileTime\":"+std::to_string(observedHostCreation)+",\"exitCode\":"+std::to_string(code)+",\"exitCodeHex\":"+Quote(codeHex)+"}",true);
            CloseHandle(hostProcess.value);hostProcess.value=nullptr;observedHost=0;
        }
        std::uint64_t hostPid=0;
        if(host!="null"&&PerformanceLog::Json(host).Unsigned("hostPid",hostPid)&&hostPid&&hostPid<=MAXDWORD&&hostPid!=observedHost){
            HANDLE candidate=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(hostPid));
            if(candidate){const auto creation=DiagnosticsSession::Creation(candidate);wchar_t hostImage[32768]{};DWORD hostLength=32768;
                const bool correctImage=QueryFullProcessImageNameW(candidate,0,hostImage,&hostLength)&&
                    (!_wcsicmp(hostImage,DiagnosticsSession::Join(package,L"KaneLynchVRHost.exe").c_str())||
                     !_wcsicmp(hostImage,DiagnosticsSession::Join(package,L"KaneLynchOpenXRHost.exe").c_str()));
                if(!correctImage||!creation||creation>PerformanceLog::FileTicks(hostRecord.written)||
                   PerformanceLog::FileTicks(hostRecord.written)<created||WaitForSingleObject(candidate,0)!=WAIT_TIMEOUT){CloseHandle(candidate);candidate=nullptr;}
                if(candidate){
                if(hostProcess.value)CloseHandle(hostProcess.value);hostProcess.value=candidate;observedHost=DWORD(hostPid);observedHostCreation=creation;
                journal.Add("{\"event\":\"host_observed\",\"utc\":"+Quote(DiagnosticsSession::Utc())+",\"hostPid\":"+std::to_string(observedHost)+
                    ",\"processCreationFileTime\":"+std::to_string(creation)+"}",true);
                }
            }
        }
        std::uint64_t presents=0;const bool hasCounter=source!="null"&&PerformanceLog::Json(source).Unsigned("presents",presents);
        bool focused=false;if(source!="null")PerformanceLog::Json(source).Boolean("gameFocused",focused);
        if(!hasCounter||!focused)counterSeen=false;
        else if(!counterSeen||!previousFocused||presents!=previousPresents){previousPresents=presents;lastProgress=now;counterSeen=true;}
        previousFocused=focused;
        const bool counterStale=hasCounter&&focused&&now-lastProgress>=3000;
        const bool stale=counterStale||(seenGame&&missingSince&&now-missingSince>=3000);
        if(stale&&!wasStale){++renderStalls;stalledPresents=presents;journal.Add("{\"event\":\"heartbeat_or_render_stall\",\"utc\":"+Quote(DiagnosticsSession::Utc())+
            ",\"elapsedMs\":"+std::to_string(now-start)+",\"statusFresh\":"+(source!="null"?"true":"false")+"}",true);}
        if(!stale&&wasStale)journal.Add(std::string("{\"event\":\"")+
            (hasCounter&&presents>stalledPresents?"recovered":"stall_no_longer_observed")+
            "\",\"utc\":"+Quote(DiagnosticsSession::Utc())+"}",true);
        wasStale=stale;
        WindowSearch window;window.pid=pid;EnumWindows(FindWindow,reinterpret_cast<LPARAM>(&window));
        if(window.hung&&!wasHung)journal.Add("{\"event\":\"window_unresponsive\",\"utc\":"+Quote(DiagnosticsSession::Utc())+"}",true);
        wasHung=window.hung;
        bool hooksReady=false;if(source!="null")PerformanceLog::Json(source).Boolean("hookInstalled",hooksReady);
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        if(hangCapture.Observe(now,SessionHangCapture::Enabled(package),hasCounter&&hooksReady,presents,focused,
            window.known,window.hung,foregroundPid==pid,stale)){
            const bool savedGame=lastGame!="null"&&Write(DiagnosticsSession::Join(directory,L"last-game.json"),lastGame+"\n");
            const bool savedHost=lastHost!="null"&&Write(DiagnosticsSession::Join(directory,L"last-host.json"),lastHost+"\n");
            // A durable marker precedes a potentially slow OS dump call. It
            // remains outside the game even if its render/driver thread hangs.
            if(journal.Add("{\"event\":\"hang_capture_attempt\",\"utc\":"+Quote(DiagnosticsSession::Utc())+","+identity+
                ",\"dumpFlags\":"+std::to_string(unsigned(SessionHangCapture::DumpType))+",\"sustainedMs\":10000"+
                ",\"lastGameSaved\":"+(savedGame?"true":"false")+",\"lastHostSaved\":"+(savedHost?"true":"false")+"}",true)){
                const auto capture=SessionHangCapture::Capture(package,pid,created,expected);
                journal.Add("{\"event\":\"hang_capture_result\",\"utc\":"+Quote(DiagnosticsSession::Utc())+","+identity+
                    ",\"written\":"+(capture.written?"true":"false")+",\"error\":"+std::to_string(capture.error)+
                    ",\"bytes\":"+std::to_string(capture.bytes)+"}",true);
            }
        }
        journal.Add("{\"event\":\"observation\",\"utc\":"+Quote(DiagnosticsSession::Utc())+",\"elapsedMs\":"+std::to_string(now-start)+
            ",\"gameStatusFresh\":"+(source!="null"?"true":"false")+",\"hostStatusFresh\":"+(host!="null"?"true":"false")+
            ",\"presents\":"+(hasCounter?std::to_string(presents):"null")+",\"windowHungKnown\":"+(window.known?"true":"false")+
            ",\"windowHung\":"+(window.hung?"true":"false")+"}");++observations;
        if(!lastSnapshot||now-lastSnapshot>=5000){
            if(lastGame!="null")Write(DiagnosticsSession::Join(directory,L"last-game.json"),lastGame+"\n");
            if(lastHost!="null")Write(DiagnosticsSession::Join(directory,L"last-host.json"),lastHost+"\n");lastSnapshot=now;
        }
        waitResult=WaitForSingleObject(game,1000);if(waitResult!=WAIT_TIMEOUT)break;
    }
    DWORD code=STILL_ACTIVE;const bool exitObserved=waitResult==WAIT_OBJECT_0&&GetExitCodeProcess(game,&code);
    const auto crash=Read(DiagnosticsSession::Join(directory,L"crash.jsonl"));
    const bool exception=crash.find("\"event\":\"exception\"")!=std::string::npos;
    const bool normalMarker=crash.find("\"event\":\"normal_end\"")!=std::string::npos;
    char hex[16]{};std::snprintf(hex,sizeof(hex),"0x%08lx",static_cast<unsigned long>(code));
    const auto summary="{\"event\":\"process_exit\",\"utc\":"+Quote(DiagnosticsSession::Utc())+","+identity+
        ",\"elapsedMs\":"+std::to_string(GetTickCount64()-start)+",\"exitCode\":"+(exitObserved?std::to_string(code):"null")+",\"exitCodeHex\":"+(exitObserved?Quote(hex):"null")+
        ",\"exitObserved\":"+(exitObserved?"true":"false")+",\"classification\":"+Quote(exitObserved?ExitKind(code,exception):"observer_wait_error")+",\"observations\":"+std::to_string(observations)+
        ",\"stallEvents\":"+std::to_string(renderStalls)+",\"maxStatusMissingMs\":"+std::to_string(maxMissing)+
        ",\"exceptionRecord\":"+(exception?"true":"false")+",\"normalEndMarker\":"+(normalMarker?"true":"false")+",\"lastGame\":"+lastGame+",\"lastHost\":"+lastHost+"}";
    journal.Add(summary,true);Write(DiagnosticsSession::Join(directory,L"summary.json"),summary+"\n");
    if(lastGame!="null")Write(DiagnosticsSession::Join(directory,L"last-game.json"),lastGame+"\n");
    if(lastHost!="null")Write(DiagnosticsSession::Join(directory,L"last-host.json"),lastHost+"\n");
    return exitObserved?0:7;
}
}
