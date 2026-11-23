#define WIN32_LEAN_AND_MEAN
#include "session_monitor.hpp"
#include <filesystem>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void Check(bool condition,const char* text){++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
void Run(const std::wstring& executable,const std::wstring& root,DWORD code){
    const auto package=DiagnosticsSession::Join(root,std::to_wstring(code));
    Check(DiagnosticsSession::Ensure(package),"create isolated fixture package");
    auto command=L"\""+executable+L"\" --fixture "+std::to_wstring(code);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    Check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,
          "start only the diagnostic fixture child");
    CloseHandle(child.hThread);SessionMonitor::Handle process;process.value=child.hProcess;
    const auto creation=DiagnosticsSession::Creation(process.value);
    Check(!SessionMonitor::Matches(process.value,executable,creation+1),"creation time rejects PID reuse/another lifetime");
    Check(!SessionMonitor::Matches(process.value,executable+L".other",creation),"image mismatch rejects another process");
    Check(SessionMonitor::Observe(process.value,child.dwProcessId,creation+1,executable,package,"test")==3,
          "observer cannot start with unqualified identity");
    const auto status="{\"pid\":"+std::to_string(child.dwProcessId)+",\"updatedTick\":"+std::to_string(GetTickCount())+
        ",\"presents\":17,\"gameFocused\":true}";
    Check(SessionMonitor::Write(DiagnosticsSession::Join(package,L"KaneLynchVR-status.json"),status),"publish fixture heartbeat");
    Check(SessionMonitor::Observe(process.value,child.dwProcessId,creation,executable,package,"test")==0,
          "retained handle observes child exit without any game or injection");
    const auto directory=DiagnosticsSession::Directory(package,child.dwProcessId,creation);
    const auto summary=SessionMonitor::Read(DiagnosticsSession::Join(directory,L"summary.json"));std::uint64_t recorded=0;
    Check(PerformanceLog::Json(summary).Unsigned("exitCode",recorded)&&recorded==code,"durable summary retains actual process exit code");
    Check(summary.find(SessionMonitor::ExitKind(code,false))!=std::string::npos,"exit classification does not invent an exception record");
    bool observed=false,normalMarker=true;
    Check(PerformanceLog::Json(summary).Boolean("exitObserved",observed)&&observed,"summary distinguishes a retained-handle exit from observer wait failure");
    Check(PerformanceLog::Json(summary).Boolean("normalEndMarker",normalMarker)&&!normalMarker,"zero exit cannot invent an explicit normal worker marker");
    Check(SessionMonitor::Read(DiagnosticsSession::Join(directory,L"session.jsonl")).find("\"event\":\"process_exit\"")!=std::string::npos,
          "session journal persists after child process has ended");
    Check(SessionMonitor::Read(DiagnosticsSession::Join(directory,L"last-game.json")).find("\"presents\":17")!=std::string::npos,
          "last qualified snapshot survives source exit");
}
struct FocusStatus {
    std::wstring package;DWORD pid=0;HANDLE game=nullptr;ULONGLONG start=0;volatile LONG failed=0;
};
DWORD WINAPI PublishFocusStatus(void* parameter){
    auto& fixture=*static_cast<FocusStatus*>(parameter);
    do{
        const bool focused=GetTickCount64()-fixture.start>=4000;
        const auto status="{\"pid\":"+std::to_string(fixture.pid)+",\"updatedTick\":"+std::to_string(GetTickCount())+
            ",\"presents\":17,\"gameFocused\":"+(focused?"true":"false")+"}";
        if(!SessionMonitor::Write(DiagnosticsSession::Join(fixture.package,L"KaneLynchVR-status.json"),status))InterlockedExchange(&fixture.failed,1);
    }while(WaitForSingleObject(fixture.game,250)==WAIT_TIMEOUT);
    return 0;
}
void FocusRecovery(const std::wstring& executable,const std::wstring& root){
    const auto package=DiagnosticsSession::Join(root,L"focus");Check(DiagnosticsSession::Ensure(package),"create isolated focus-recovery package");
    auto command=L"\""+executable+L"\" --fixture 0 5600";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    Check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,
          "start only a short-lived focus-recovery fixture child");
    CloseHandle(child.hThread);SessionMonitor::Handle process;process.value=child.hProcess;
    FocusStatus fixture;fixture.package=package;fixture.pid=child.dwProcessId;fixture.game=process.value;fixture.start=GetTickCount64();
    HANDLE writer=CreateThread(nullptr,0,PublishFocusStatus,&fixture,0,nullptr);Check(writer!=nullptr,"publish fresh heartbeat while fixture is intentionally unfocused");
    const auto creation=DiagnosticsSession::Creation(process.value);
    Check(SessionMonitor::Observe(process.value,child.dwProcessId,creation,executable,package,"test")==0,"observer follows four-second unfocus and a short refocus without touching the child");
    if(writer){WaitForSingleObject(writer,2000);CloseHandle(writer);}
    Check(!fixture.failed,"all isolated focus heartbeat publications succeeded");
    const auto summary=SessionMonitor::Read(DiagnosticsSession::Join(DiagnosticsSession::Directory(package,child.dwProcessId,creation),L"summary.json"));
    std::uint64_t stalls=99;
    Check(PerformanceLog::Json(summary).Unsigned("stallEvents",stalls)&&stalls==0,
          "refocus after four seconds without presents starts a fresh stall interval instead of inventing a hang");
}
}
int wmain(int argc,wchar_t** argv){
    if((argc==3||argc==4)&&!std::wcscmp(argv[1],L"--fixture")){Sleep(argc==4?static_cast<DWORD>(std::wcstoul(argv[3],nullptr,10)):400);ExitProcess(static_cast<DWORD>(std::wcstoull(argv[2],nullptr,10)));}
    wchar_t executable[32768]{},temporary[32768]{};GetModuleFileNameW(nullptr,executable,32768);GetTempPathW(32768,temporary);
    const std::filesystem::path parent=std::filesystem::weakly_canonical(temporary);
    const auto root=parent/(L"klmon-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Check(std::filesystem::create_directory(root),"create a fresh unique temporary root");
    const auto bounded=DiagnosticsSession::Join(root.wstring(),L"bounded-status.json");
    const std::string bigStatus(600000,'x');
    Check(SessionMonitor::Write(bounded,bigStatus)&&SessionMonitor::Read(bounded)==bigStatus,
          "monitor retains expanded status/summary payload above old 256KB bound");
    const std::string oversized(static_cast<std::size_t>(2*PerformanceLog::MaxSnapshotBytes+65536)+1,'x');
    Check(SessionMonitor::Write(bounded,oversized)&&SessionMonitor::Read(bounded).empty(),
          "monitor rejects payload above bounded two-snapshot summary limit");
    Run(executable,root.wstring(),0);Run(executable,root.wstring(),7);Run(executable,root.wstring(),0xc0000005u);
    FocusRecovery(executable,root.wstring());
    Check(std::string(SessionMonitor::ExitKind(0,true))=="recorded_exception","recorded crash is distinct from a clean exit code");
    Check(std::string(SessionMonitor::ExitKind(0,false))=="zero_exit_without_recorded_exception","exit code zero without an exception record does not claim a proven normal exit");
    Check(root.parent_path()==parent&&root.filename().wstring().rfind(L"klmon-test-",0)==0,"cleanup remains inside the exact fixture directory");
    std::filesystem::remove_all(root);
    std::printf("Session observer: %u checks passed; diagnostic child fixtures only.\n",checks);return 0;
}
