#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crash_diagnostics.hpp"
#include <cstdio>
#include <vector>

namespace {
unsigned checks=0,failures=0;
void Check(bool okay,const char* message){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",message);}}
HANDLE previousMarker=INVALID_HANDLE_VALUE;
LONG WINAPI Previous(EXCEPTION_POINTERS*){constexpr char line[]="previous_filter\n";DWORD written=0;if(previousMarker!=INVALID_HANDLE_VALUE){WriteFile(previousMarker,line,sizeof(line)-1,&written,nullptr);FlushFileBuffers(previousMarker);}return EXCEPTION_CONTINUE_SEARCH;}
LONG WINAPI Replacement(EXCEPTION_POINTERS*){return EXCEPTION_CONTINUE_SEARCH;}
DWORD WINAPI OtherThread(void*){CrashDiagnostics::Breadcrumb("fixture_upload",320,77);return 0;}
volatile std::uintptr_t badAddress=1;
__declspec(noinline) void CrashNow(){*reinterpret_cast<volatile unsigned*>(badAddress)=0x12345678;}
bool Read(const std::wstring& path,std::string& bytes){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};bool okay=GetFileSizeEx(file,&size)&&size.QuadPart>=0&&size.QuadPart<=64*1024*1024;
    if(okay){bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD got=0;okay=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size();}CloseHandle(file);return okay;}
int Child(const std::wstring& mode,const std::wstring& base){
    // Test-process-only error UI suppression; production diagnostics changes
    // neither WER policy/registry nor the game's error-mode flags.
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    const auto directory=DiagnosticsSession::CurrentDirectory(base);if(directory.empty())return 2;
    previousMarker=CreateFileW((directory+L"\\previous.marker").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    SetUnhandledExceptionFilter(Previous);
    if(!CrashDiagnostics::Install(base,"fixture-0.3.21"))return 3;
    if(!CrashDiagnostics::Active()||CrashDiagnostics::Directory()!=directory)return 4;
    if(mode==L"--child-crash"){
        HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);if(thread){WaitForSingleObject(thread,1000);CloseHandle(thread);}
        CrashDiagnostics::Breadcrumb("fixture_present",321,(std::uint64_t(3840)<<32)|2160);
        CrashNow();return 10;
    }
    if(mode==L"--child-replaced")SetUnhandledExceptionFilter(Replacement);
    CrashDiagnostics::Breadcrumb("fixture_normal",123,456);CrashDiagnostics::Close("fixture_normal");
    const auto previous=SetUnhandledExceptionFilter(nullptr);
    if(previous!=(mode==L"--child-replaced"?Replacement:Previous))return 5;
    if(previousMarker!=INVALID_HANDLE_VALUE)CloseHandle(previousMarker);
    return CrashDiagnostics::Active()?6:0;
}
void RemoveOwnTree(const std::wstring& directory){WIN32_FIND_DATAW entry{};HANDLE find=FindFirstFileW((directory+L"\\*").c_str(),&entry);
    if(find!=INVALID_HANDLE_VALUE){do{if(!wcscmp(entry.cFileName,L".")||!wcscmp(entry.cFileName,L".."))continue;const auto path=directory+L"\\"+entry.cFileName;
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))RemoveOwnTree(path);}else DeleteFileW(path.c_str());
    }while(FindNextFileW(find,&entry));FindClose(find);}RemoveDirectoryW(directory.c_str());}
struct TemporaryFolder {
    std::wstring path;
    TemporaryFolder(){wchar_t base[MAX_PATH]{},name[MAX_PATH]{};if(GetTempPathW(MAX_PATH,base)&&GetTempFileNameW(base,L"klc",0,name)){DeleteFileW(name);if(CreateDirectoryW(name,nullptr))path=name;}}
    ~TemporaryFolder(){if(!path.empty()){if(!failures)RemoveOwnTree(path);else std::wprintf(L"Fixture artifacts retained at %ls\n",path.c_str());}}
};
struct Run {DWORD exit=0,pid=0;std::wstring directory;bool completed=false;};
Run Spawn(const std::wstring& executable,const std::wstring& mode,const std::wstring& base){
    Run run;std::wstring command=L"\""+executable+L"\" "+mode+L" \""+base+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,base.c_str(),&startup,&process))return run;
    run.pid=process.dwProcessId;run.directory=DiagnosticsSession::Directory(base,run.pid,DiagnosticsSession::Creation(process.hProcess));
    CloseHandle(process.hThread);run.completed=WaitForSingleObject(process.hProcess,12000)==WAIT_OBJECT_0;
    if(!run.completed){TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,1000);}
    GetExitCodeProcess(process.hProcess,&run.exit);CloseHandle(process.hProcess);return run;
}
bool DumpHasFault(const std::string& bytes){
    if(bytes.size()<sizeof(MINIDUMP_HEADER))return false;
    const auto* header=reinterpret_cast<const MINIDUMP_HEADER*>(bytes.data());
    if(header->Signature!=MINIDUMP_SIGNATURE||header->NumberOfStreams>128||
       header->StreamDirectoryRva>bytes.size()||std::uint64_t(header->NumberOfStreams)*sizeof(MINIDUMP_DIRECTORY)>bytes.size()-header->StreamDirectoryRva)return false;
    const auto* streams=reinterpret_cast<const MINIDUMP_DIRECTORY*>(bytes.data()+header->StreamDirectoryRva);
    const MINIDUMP_EXCEPTION_STREAM* exception=nullptr;const MINIDUMP_MODULE_LIST* modules=nullptr;std::size_t moduleBytes=0;
    for(unsigned i=0;i<header->NumberOfStreams;++i){const auto& stream=streams[i];
        if(stream.Location.Rva>bytes.size()||stream.Location.DataSize>bytes.size()-stream.Location.Rva)return false;
        if(stream.StreamType==ExceptionStream&&stream.Location.DataSize>=sizeof(MINIDUMP_EXCEPTION_STREAM))exception=reinterpret_cast<const MINIDUMP_EXCEPTION_STREAM*>(bytes.data()+stream.Location.Rva);
        if(stream.StreamType==ModuleListStream&&stream.Location.DataSize>=sizeof(ULONG32)){modules=reinterpret_cast<const MINIDUMP_MODULE_LIST*>(bytes.data()+stream.Location.Rva);moduleBytes=stream.Location.DataSize;}
    }
    if(!exception||exception->ExceptionRecord.ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||!exception->ThreadId||
       exception->ExceptionRecord.NumberParameters<2||exception->ExceptionRecord.ExceptionInformation[1]!=1||!modules||
       std::uint64_t(modules->NumberOfModules)*sizeof(MINIDUMP_MODULE)+sizeof(ULONG32)>moduleBytes)return false;
    const auto& contextLocation=exception->ThreadContext;
    if(contextLocation.DataSize<sizeof(CONTEXT)||contextLocation.Rva>bytes.size()||contextLocation.DataSize>bytes.size()-contextLocation.Rva)return false;
    const auto* context=reinterpret_cast<const CONTEXT*>(bytes.data()+contextLocation.Rva);
    if((context->ContextFlags&CONTEXT_CONTROL)!=CONTEXT_CONTROL)return false;
    const auto address=exception->ExceptionRecord.ExceptionAddress;
    for(unsigned i=0;i<modules->NumberOfModules;++i){const auto& module=modules->Modules[i];if(address>=module.BaseOfImage&&address-module.BaseOfImage<module.SizeOfImage)return true;}
    return false;
}
void VerifyChild(const std::wstring& executable,const std::wstring& mode,const std::wstring& base){
    const auto run=Spawn(executable,mode,base);Check(run.pid&&run.completed,"isolated child exits within the fixture timeout");if(!run.pid||!run.completed)return;
    std::string log;Check(Read(run.directory+L"\\crash.jsonl",log)&&!log.empty()&&log.size()<=65536&&log.back()=='\n',"preopened bounded crash JSONL survives child termination");
    Check(log.find("\"event\":\"crash_session\"")!=std::string::npos&&log.find("\"pid\":"+std::to_string(run.pid))!=std::string::npos&&
          log.find("\"processCreationFt\":")!=std::string::npos,"crash artifacts carry the same PID and process creation identity as their session directory");
    if(mode==L"--child-crash"){
        Check(run.exit==EXCEPTION_ACCESS_VIOLATION,"diagnostics does not swallow or convert the game's unhandled access violation");
        Check(log.find("\"event\":\"exception\"")!=std::string::npos&&log.find("\"exceptionCode\":\"0xc0000005\"")!=std::string::npos,
              "fatal JSONL records the real exception code and address");
        const auto leaf=executable.substr(executable.find_last_of(L"\\/")+1);
        Check(log.find(DiagnosticsSession::Utf8(leaf))!=std::string::npos&&log.find("\"moduleRva\":\"0x")!=std::string::npos,
              "exception record resolves its preloaded module and module-relative fault address");
        Check(log.find("\"stage\":\"fixture_present\",\"frame\":321")!=std::string::npos&&log.find("\"stage\":\"fixture_upload\",\"frame\":320")!=std::string::npos,
              "RAM-only per-thread crumbs retain both fault-thread and recent upload stage/frame");
        Check(log.find("\"event\":\"minidump\"")!=std::string::npos&&log.find("\"written\":true")!=std::string::npos,
              "dedicated dump worker completes its best-effort minidump");
        std::string dump;Check(Read(run.directory+L"\\game.dmp",dump)&&DumpHasFault(dump),"minidump contains a valid exception stream and matching fault module");
        std::string marker;Check(Read(run.directory+L"\\previous.marker",marker)&&marker=="previous_filter\n", "previous unhandled filter is chained exactly once");
        Check(log.find("normal_end")==std::string::npos,"fatal session cannot be mislabeled as a normal worker exit");
    }else{
        Check(run.exit==0,"explicit normal close restores the previous or subsequently replaced application filter");
        Check(log.find("\"event\":\"normal_end\"")!=std::string::npos&&log.find("\"reason\":\"fixture_normal\"")!=std::string::npos&&log.find("\"event\":\"exception\"")==std::string::npos,
              "normal exit has an explicit marker and no invented exception");
        Check(GetFileAttributesW((run.directory+L"\\game.dmp").c_str())==INVALID_FILE_ATTRIBUTES,"normal runs never create an empty dump artifact");
    }
}
}
int wmain(int argc,wchar_t** argv){
    if(argc==3&&(std::wstring(argv[1])==L"--child-crash"||std::wstring(argv[1])==L"--child-normal"||std::wstring(argv[1])==L"--child-replaced"))return Child(argv[1],argv[2]);
    TemporaryFolder temporary;if(temporary.path.empty())return 2;wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))return 2;
    VerifyChild(executable,L"--child-normal",temporary.path);VerifyChild(executable,L"--child-replaced",temporary.path);VerifyChild(executable,L"--child-crash",temporary.path);
    std::printf("Crash diagnostics child fixtures: %u checks, %u failures; no game or headset session opened.\n",checks,failures);return failures?1:0;
}
