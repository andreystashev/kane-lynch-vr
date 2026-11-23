// LaunchVR.exe (x86): starts Kane & Lynch: Dead Men and injects
// KaneLynchVR.dll, following the Blood Money / Contracts launcher's process
// lifecycle and checks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include "profile.hpp"
#include "audio_startup.hpp"
#include "game_renderer.hpp"
#include "diagnostics_launch.hpp"
#include "launcher_startup_trace.hpp"
#include "launcher_startup_wait.hpp"
#include "launcher_startup_route.hpp"
#include "launcher_native_startup.hpp"
#include "launcher_steam_client.hpp"
#include "launcher_steam_retry.hpp"

namespace {
class LaunchLock {
    HANDLE mutex_=CreateMutexW(nullptr,FALSE,L"Local\\KaneLynchVR-Launch");
    bool acquired_=false;
public:
    LaunchLock() {
        if(mutex_) {
            const auto wait=WaitForSingleObject(mutex_,0);
            acquired_=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
        }
    }
    ~LaunchLock() {
        if(acquired_)ReleaseMutex(mutex_);
        if(mutex_)CloseHandle(mutex_);
    }
    explicit operator bool() const {return acquired_;}
};

constexpr const wchar_t* kGameExeName=L"KANEANDLYNCH.EXE";
enum class ExistingModule { Missing, SamePath, OtherPath, Unavailable };

std::vector<DWORD> GamePids() {
    std::vector<DWORD> pids;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return pids;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snapshot,&entry))do {
        if(_wcsicmp(entry.szExeFile,kGameExeName)==0)pids.push_back(entry.th32ProcessID);
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return pids;
}
// Retrying a failed native bootstrap is allowed only after a complete, fresh
// enumeration proves that no game appeared during its exit grace interval.
bool NoGameProcess() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    bool none=true;bool enumerated=Process32FirstW(snapshot,&entry)!=FALSE;
    if(enumerated)do {
        if(!_wcsicmp(entry.szExeFile,kGameExeName)){none=false;break;}
    }while(Process32NextW(snapshot,&entry));
    const bool complete=none&&GetLastError()==ERROR_NO_MORE_FILES;
    CloseHandle(snapshot);return enumerated&&complete;
}
bool RegularFile(const std::wstring& path) {
    const DWORD attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring NormalizePath(std::wstring path) {
    for(wchar_t& c:path)if(c==L'/')c=L'\\';
    if(path.compare(0,4,L"\\\\?\\")==0)path.erase(0,4);
    return path;
}

bool SamePath(const std::wstring& a,const std::wstring& b) {
    const auto first=NormalizePath(a),second=NormalizePath(b);
    return _wcsicmp(first.c_str(),second.c_str())==0;
}

DWORD ParsePid(const wchar_t* argument) {
    if(!argument||!argument[0]||argument[0]<L'0'||argument[0]>L'9')return 0;
    wchar_t* end=nullptr;errno=0;
    const unsigned long value=wcstoul(argument,&end,10);
    return !errno&&end!=argument&&!*end&&value>0&&value<=ULONG_MAX?static_cast<DWORD>(value):0;
}

ExistingModule InspectModule(DWORD pid,const std::wstring& expected) {
    const auto normalized=NormalizePath(expected);
    const wchar_t* name=wcsrchr(normalized.c_str(),L'\\');
    if(!name)return ExistingModule::Unavailable;
    ++name;
    HANDLE snapshot=INVALID_HANDLE_VALUE;
    for(int attempt=0;attempt<8;++attempt) {
        snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
        if(snapshot!=INVALID_HANDLE_VALUE||GetLastError()!=ERROR_BAD_LENGTH)break;
        Sleep(1);
    }
    if(snapshot==INVALID_HANDLE_VALUE)return ExistingModule::Unavailable;
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
    ExistingModule result=ExistingModule::Missing;
    if(Module32FirstW(snapshot,&entry))do {
        if(_wcsicmp(entry.szModule,name)==0) {
            result=SamePath(entry.szExePath,expected)?ExistingModule::SamePath:ExistingModule::OtherPath;
            if(result==ExistingModule::OtherPath)break;
        }
    }while(Module32NextW(snapshot,&entry));
    else result=ExistingModule::Unavailable;
    CloseHandle(snapshot);return result;
}

bool RemoteLoad(HANDLE process,const wchar_t* path) {
    const SIZE_T size=(wcslen(path)+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(process,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote)return false;
    SIZE_T written=0;
    if(!WriteProcessMemory(process,remote,path,size,&written)||written!=size) {
        VirtualFreeEx(process,remote,0,MEM_RELEASE);return false;
    }
    const auto load=reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"));
    HANDLE thread=load?CreateRemoteThread(process,nullptr,0,load,remote,0,nullptr):nullptr;
    if(!thread){VirtualFreeEx(process,remote,0,MEM_RELEASE);return false;}
    const DWORD wait=WaitForSingleObject(thread,15000);DWORD result=0;
    if(wait==WAIT_OBJECT_0) {
        GetExitCodeThread(thread,&result);
        VirtualFreeEx(process,remote,0,MEM_RELEASE);
    }
    CloseHandle(thread);return wait==WAIT_OBJECT_0&&result!=0;
}

struct Package {
    std::wstring folder,module;
};

bool FindPackage(Package& package) {
    wchar_t path[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* tail=wcsrchr(path,L'\\');
    if(!length||length>=MAX_PATH||!tail) {
        std::puts("Cannot resolve the launcher's directory.");return false;
    }
    package.folder.assign(path,static_cast<size_t>(tail-path+1));
    package.module=package.folder+L"KaneLynchVR.dll";
    if(!RegularFile(package.module)) {
        std::wprintf(L"Required file is missing beside the launcher: %ls\n",package.module.c_str());return false;
    }
    return true;
}

void ArmRecorder(const Package& package,DWORD pid) {
    const auto script=package.folder+L"Start-KL1-Recorder.ps1";
    if(!RegularFile(script)||!GetPrivateProfileIntW(L"Recording",L"Enabled",0,(package.folder+L"vr.ini").c_str()))return;
    wchar_t system[MAX_PATH]{};const auto length=GetSystemDirectoryW(system,MAX_PATH);
    if(!length||length>=MAX_PATH)return;
    const auto powershell=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    auto command=L"\""+powershell+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+script+L"\" -GamePid "+std::to_wstring(pid);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION recorder{};
    if(CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,package.folder.c_str(),&startup,&recorder)){
        CloseHandle(recorder.hThread);CloseHandle(recorder.hProcess);
        std::puts("Recorder startup requested: Ctrl+F10 starts, Ctrl+F11 saves, Ctrl+F12 exits the recorder.");
    }else std::puts("Recorder could not be armed; use Record-KaneLynch-VR.cmd. The game continues.");
}

bool LoadPackage(HANDLE process,DWORD pid,const Package& package) {
    const ExistingModule module=InspectModule(pid,package.module);
    if(module==ExistingModule::OtherPath) {
        std::puts("A KaneLynchVR.dll from another folder is already loaded. Restart the game using this launcher to avoid duplicate hooks.");return false;
    }
    if(module==ExistingModule::Unavailable) {
        std::puts("Cannot inspect the game's loaded modules. No DLL loaded.");return false;
    }
    if(module==ExistingModule::Missing&&!RemoteLoad(process,package.module.c_str())) {
        std::puts("KaneLynchVR.dll loading failed or timed out. Check KaneLynchVR.log.");return false;
    }
    return true;
}

bool SupportedProcess(HANDLE process,bool* steam=nullptr) {
    wchar_t exe[MAX_PATH]{};DWORD length=MAX_PATH;char digest[65]{};
    if(QueryFullProcessImageNameW(process,0,exe,&length)&&HashSupportedGameFile(exe,digest)){
        if(steam)*steam=std::strcmp(digest,kSupportedSha256)!=0;return true;
    }
    std::printf("Unsupported process EXE. SHA256: %s\nNo DLLs loaded.\n",digest);return false;
}

void PrintReady() {
    std::puts("KaneLynchVR.dll loaded.");
    std::puts("F9 toggles the selected headset output. VR starts automatically when AutoStart=1 in vr.ini.");
}

int SelfTest() {
    const bool ok=ParsePid(L"42")==42&&ParsePid(L"0")==0&&ParsePid(L"-1")==0&&
        ParsePid(L"+42")==0&&ParsePid(L"42junk")==0&&ParsePid(L"4294967296")==0&&
        SamePath(L"C:\\VR\\KaneLynchVR.dll",L"c:/vr/kanelynchvr.dll")&&
        SamePath(L"\\\\?\\C:\\VR\\KaneLynchVR.dll",L"C:\\VR\\KaneLynchVR.dll")&&
        !SamePath(L"C:\\old\\KaneLynchVR.dll",L"C:\\VR\\KaneLynchVR.dll");
    std::puts(ok?"Launcher PID/path tests passed; no game or VR runtime opened.":"Launcher PID/path tests failed.");
    return ok?0:12;
}
}

int wmain(int argc,wchar_t** argv) {
    if(argc==2&&wcscmp(argv[1],L"--self-test")==0)return SelfTest();
    if(argc==2&&wcscmp(argv[1],L"--audio-check")==0) {
        AudioStartup::Endpoint endpoint;
        if(!AudioStartup::Select(L"Auto",endpoint)){std::puts("No unambiguous active headset audio output found. No settings changed.");return 13;}
        wchar_t guid[64]{};StringFromGUID2(endpoint.guid,guid,64);
        std::wprintf(L"Selected headset audio: %ls\nEndpoint: %ls\nDirectSound GUID: %ls\nNo settings changed.\n",endpoint.name.c_str(),endpoint.id.c_str(),guid);return 0;
    }
    if(argc==2&&(wcscmp(argv[1],L"--help")==0||wcscmp(argv[1],L"-h")==0)) {
        std::puts("Usage: LaunchVR.exe [--check-installation] <kaneandlynch.exe>\nOther options: --attach PID, --self-test, --audio-check.\nThe DLL loads only after the complete native image matches a verified layout.");return 0;
    }
    if(argc==2&&wcscmp(argv[1],L"--check-vr")==0) {
        std::puts("Use KaneLynchVRHost.exe --check for the headset check.");return 0;
    }
    Package package{};
    if(argc==3&&wcscmp(argv[1],L"--attach")==0) {
        const DWORD pid=ParsePid(argv[2]);
        if(!pid){std::puts("--attach requires a positive numeric process ID.");return 1;}
        if(!FindPackage(package))return 3;
        HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid);
        if(!process){std::printf("OpenProcess failed: %lu\n",GetLastError());return 1;}
        bool steam=false;
        if(!SupportedProcess(process,&steam)){CloseHandle(process);return 2;}
        if(steam){CloseHandle(process);std::puts("Steam native compatibility requires a fresh launch. Close the game and start it with LaunchVR.exe; --attach did not load any DLL.");return 15;}
        DiagnosticsLaunch::Start(package.folder,process);
        const bool ok=LoadPackage(process,pid,package);if(ok)ArmRecorder(package,pid);CloseHandle(process);
        if(ok)PrintReady();
        return ok?0:7;
    }
    const bool checkOnly=argc==3&&wcscmp(argv[1],L"--check-installation")==0;
    if(argc!=2&&!checkOnly){std::puts("Use LaunchVR.exe --help for launch options.");return 1;}
    wchar_t exe[MAX_PATH]{};
    const DWORD length=GetFullPathNameW(argv[checkOnly?2:1],MAX_PATH,exe,nullptr);
    if(!length||length>=MAX_PATH)return 1;
    Kl1ExecutableIdentity identity;char digest[65]{};
    if(!InspectExecutable(exe,identity)||!identity.supported) {
        std::memcpy(digest,identity.sha256,65);
        std::printf("Unsupported EXE. SHA256: %s\nNo game launched or patched.\n",digest);return 2;
    }
    std::memcpy(digest,identity.sha256,65);
    if(identity.metadataVariant)std::puts("Warning: new exact EXE hash; complete code/data/resources/layout match, with only audited PE metadata differences. Gameplay on this variant is untested.");
    char officialHash[65]{};
    const auto official=LauncherStartupRoute::ReadNativeLauncher(exe,officialHash);
    const auto nativeRoute=LauncherStartupRoute::Select(digest,false,officialHash);
    if(nativeRoute.kind==LauncherStartupRoute::Kind::Invalid) {
        std::puts("The selected installation's native launcher is missing or does not match a supported edition. Keep the original files from that installation.");
        return 2;
    }
    if(checkOnly) {
        std::printf("Supported game EXE: %s\n",digest);
        std::printf("Startup route: %s\n",nativeRoute.needsSteam?"Steam launcher":
                    nativeRoute.kind==LauncherStartupRoute::Kind::OfficialLauncher?"native launcher":"direct game");
        return 0;
    }
    if(!FindPackage(package))return 3;
    LauncherStartupTrace::Run startupTrace;
    startupTrace.Begin(package.folder,exe,digest);
    const LaunchLock lock;
    if(!lock) {
        std::puts("Another Kane & Lynch VR launch is in progress. Wait for it to finish.");
        return startupTrace.Result(8);
    }
    if(!NoGameProcess()) {
        startupTrace.Stage("already_running_or_enumeration_failed");
        std::puts("Close Kane & Lynch before launching the mod. If it is closed, check process permissions and retry.");
        return startupTrace.Result(8);
    }
    if(nativeRoute.needsSteam) {
        startupTrace.Stage("steam_client_prepare");
        std::wstring steamError;
        if(!LauncherSteamClient::Ensure(steamError)) {
            startupTrace.Stage("steam_client_not_ready",steamError);
            std::fwprintf(stderr,L"%ls\\n",steamError.c_str());
            return startupTrace.Result(16);
        }
        startupTrace.Stage("steam_client_ready");
    }

    GameRenderer::Kind renderer;
    std::wstring rendererError;
    startupTrace.Stage("renderer_prepare");
    if(!GameRenderer::Prepare(package.folder,exe,renderer,rendererError)) {
        startupTrace.Stage("renderer_failed",rendererError,0,GetLastError());
        std::fwprintf(stderr,L"%ls\\n",rendererError.c_str());
        return startupTrace.Result(9);
    }
    startupTrace.Stage("renderer_ready",RendererIdentity::Name(renderer));
    wchar_t dir[MAX_PATH]{};wcscpy(dir,exe);wchar_t* tail=wcsrchr(dir,L'\\');
    if(!tail)return startupTrace.Result(1);*tail=0;
    const std::vector<DWORD> before=GamePids();
    if(!before.empty())return startupTrace.Result(8);

    AudioBootstrap::Request audio;
    const bool canRouteAudio=LauncherStartupRoute::Select(digest,true,officialHash).attachAudio;
    const bool audioPrepared=canRouteAudio&&AudioStartup::Prepare(package.folder,exe,audio);
    const auto route=LauncherStartupRoute::Select(digest,audioPrepared,officialHash);
    const bool prepareAudio=route.attachAudio;
    const bool knownEventLauncher=route.kind==LauncherStartupRoute::Kind::OfficialLauncher&&
        (!std::strcmp(officialHash,LauncherStartupRoute::GogLauncherSha)||
         !std::strcmp(officialHash,AudioBootstrap::OfficialSteamLauncherSha));
    AudioBootstrap::Request renderOnly;
    const bool prepareEvents=!prepareAudio&&knownEventLauncher&&AudioStartup::PrepareBootstrap(package.folder,exe,renderOnly);
    const bool prepareEarly=prepareAudio||prepareEvents;
    const std::wstring launchExe=route.kind==LauncherStartupRoute::Kind::OfficialLauncher?official:std::wstring(exe);
    startupTrace.Stage("native_route",launchExe,0,0,0,prepareAudio);
    std::wstring command=L"\""+launchExe+L"\"";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    LauncherStartupWait::Result startedGame;
    const auto nativeStarted=GetTickCount64();unsigned nativeRetries=0;
    for(;;){
    pi={};
    if(nativeRetries&&!NoGameProcess()){
        startupTrace.Stage("steam_native_retry_cancelled",L"A game appeared before the next native launch.");
        std::puts("A game appeared while Steam was preparing. No second game was launched or VR DLL loaded.");
        return startupTrace.Result(8);
    }
    command=L"\""+launchExe+L"\"";
    if(!CreateProcessW(launchExe.c_str(),command.data(),nullptr,nullptr,FALSE,prepareEarly?CREATE_SUSPENDED:0,nullptr,dir,&si,&pi)) {
        startupTrace.Stage("create_failed",launchExe,0,GetLastError());
        std::printf("CreateProcess failed: %lu\n",GetLastError());return startupTrace.Result(4);
    }
    startupTrace.Stage("native_created",launchExe,pi.dwProcessId);
    if(prepareEarly) {
        const bool attached=AudioBootstrap::Attach(pi.hProcess,prepareAudio?audio:renderOnly);
        startupTrace.Stage(prepareAudio?"audio_attached":"render_events_bootstrap",launchExe,pi.dwProcessId,attached?0:GetLastError(),0,attached);
        if(!attached)std::puts("Early bootstrap could not be installed. Continuing native startup.");
        if(ResumeThread(pi.hThread)==static_cast<DWORD>(-1)) {
            const auto error=GetLastError();
            std::printf("Could not resume the new game process: %lu\n",error);
            startupTrace.Stage("resume_failed",launchExe,pi.dwProcessId,error);
            TerminateProcess(pi.hProcess,4);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return startupTrace.Result(4);
        }
    }
    CloseHandle(pi.hThread);WaitForInputIdle(pi.hProcess,15000);
    LauncherStartupWait::Options wait;
    wait.initialProcess=pi.hProcess;wait.initialPid=pi.dwProcessId;
    wait.initialCreation=DiagnosticsSession::Creation(pi.hProcess);
    wait.selected=exe;wait.expectedHash=digest;wait.before=before;
    wait.initialMayBeGame=SamePath(launchExe,exe);
    startedGame=LauncherStartupWait::Wait(wait);
    startupTrace.Stage("game_wait_result",L"polls="+std::to_wstring(startedGame.polls)+
        L" rejectedCandidates="+std::to_wstring(startedGame.rejected),startedGame.pid,startedGame.error,
        startedGame.nativeExitCode,startedGame.outcome==LauncherStartupWait::Outcome::Ready);
    CloseHandle(pi.hProcess);
    pi.hProcess=nullptr;
    if(LauncherSteamRetry::Eligible(route.needsSteam,
        startedGame.outcome==LauncherStartupWait::Outcome::NativeExited,
        startedGame.nativeExitCode,startedGame.rejected,startedGame.process!=nullptr,
        nativeRetries,GetTickCount64()-nativeStarted)&&NoGameProcess()) {
        // Nothing has been injected into a game. Keep the original launcher,
        // early audio setup and Steam authorization on each fresh attempt.
        startupTrace.Stage("steam_native_retry_wait",L"native exit 53; retry="+std::to_wstring(nativeRetries+1),
            pi.dwProcessId,0,startedGame.nativeExitCode);
        std::puts("Steam is still preparing its native game service. Waiting before retrying the original launcher...");
        const auto until=GetTickCount64()+LauncherSteamRetry::DelayMs;
        bool noGame=true;
        do {
            Sleep(250);
            if(!NoGameProcess()){noGame=false;break;}
        }while(GetTickCount64()<until);
        if(!noGame){
            startupTrace.Stage("steam_native_retry_cancelled",L"A game appeared or process enumeration became unavailable.");
            std::puts("Game startup changed while Steam was preparing. No second game was launched or VR DLL loaded; close the game before trying again.");
            return startupTrace.Result(8);
        }
        std::wstring steamError;
        if(!LauncherSteamClient::Ensure(steamError)){
            startupTrace.Stage("steam_client_not_ready",steamError);std::fwprintf(stderr,L"%ls\n",steamError.c_str());return startupTrace.Result(16);
        }
        ++nativeRetries;startupTrace.Stage("steam_native_retry",L"attempt="+std::to_wstring(nativeRetries+1));
        continue;
    }
    break;
    }
    if(startedGame.outcome!=LauncherStartupWait::Outcome::Ready){
        if(startedGame.outcome==LauncherStartupWait::Outcome::NativeExited){
            startupTrace.Stage("native_exited_before_mod",launchExe,pi.dwProcessId,startedGame.error,startedGame.nativeExitCode);
            std::printf("The native startup process exited before the selected game appeared (exit 0x%08lx). No VR DLL loaded.\n",startedGame.nativeExitCode);
            if(route.needsSteam&&startedGame.nativeExitCode==53)
                std::puts("Steam's original bootstrap is still unavailable after the bounded startup wait. Finish any Steam login/update prompt, then try again; original game files were preserved.");
            return startupTrace.Result(5);
        }
        if(startedGame.outcome==LauncherStartupWait::Outcome::TimedOut){
            startupTrace.Stage("selected_game_timeout",exe);
            std::puts("The selected game did not appear within 20 seconds. The native launcher was not treated as the game; no VR DLL loaded.");
            return startupTrace.Result(14);
        }
        startupTrace.Stage("selected_game_wait_failed",exe,0,startedGame.error);
        std::printf("Cannot qualify the selected game's startup process (Windows error %lu). No VR DLL loaded.\n",startedGame.error);
        return startupTrace.Result(8);
    }
    pi.hProcess=startedGame.process;pi.dwProcessId=startedGame.pid;
    if(startedGame.rebound){
        std::printf("Selected game appeared after native startup; attaching to PID %lu.\n",pi.dwProcessId);
        startupTrace.Stage("game_rebound",exe,pi.dwProcessId);
    }
    if(!SupportedProcess(pi.hProcess)){startupTrace.Stage("game_identity_rejected",exe,pi.dwProcessId);CloseHandle(pi.hProcess);return startupTrace.Result(2);}
    startupTrace.Stage("game_identity_verified",exe,pi.dwProcessId);
    DiagnosticsLaunch::Start(package.folder,pi.hProcess);
    if(!LauncherStartupWait::MatchesSelected(pi.hProcess,exe,digest,startedGame.creation,startedGame.creation)){
        DWORD exited=0;GetExitCodeProcess(pi.hProcess,&exited);
        startupTrace.Stage("selected_game_lost_before_injection",exe,pi.dwProcessId,0,exited);
        CloseHandle(pi.hProcess);std::puts("The selected game ended or changed identity before VR injection. No VR DLL loaded.");
        return startupTrace.Result(5);
    }
    const bool steam=std::strcmp(digest,kSupportedSha256)!=0;
    LauncherNativeStartup::Channel nativeStartup;
    if(steam&&!nativeStartup.CreateFresh(pi.dwProcessId,startedGame.creation)){
        startupTrace.Stage("native_compatibility_channel_failed",exe,pi.dwProcessId,GetLastError());
        CloseHandle(pi.hProcess);std::puts("Cannot create a fresh native compatibility channel. No VR DLL loaded; the native game was left running.");
        return startupTrace.Result(15);
    }
    startupTrace.Stage("mod_injection_begin",package.module,pi.dwProcessId);
    const bool ok=LoadPackage(pi.hProcess,pi.dwProcessId,package);
    startupTrace.Stage("mod_injection_end",package.module,pi.dwProcessId,ok?0:GetLastError(),0,ok);
    if(steam){
        const auto native=ok?nativeStartup.Wait(pi.hProcess,20000):LauncherNativeStartup::Outcome::Invalid;
        const bool ready=native==LauncherNativeStartup::Outcome::Ready&&
            LauncherStartupWait::MatchesSelected(pi.hProcess,exe,digest,startedGame.creation,startedGame.creation);
        startupTrace.Stage("native_compatibility_result",L"outcome="+std::to_wstring(static_cast<unsigned>(native)),pi.dwProcessId,0,0,ready);
        if(!ready){
            // Only the exact fresh game qualified above belongs to this launch.
            // Existing games (--attach) and headset/host startup never enter here.
            bool stopped=false;
            HANDLE owned=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pi.dwProcessId);
            if(owned&&GetProcessId(owned)==pi.dwProcessId&&
               LauncherStartupWait::MatchesSelected(owned,exe,digest,startedGame.creation,startedGame.creation)){
                stopped=TerminateProcess(owned,15)!=FALSE;if(stopped)WaitForSingleObject(owned,5000);
            }
            if(owned)CloseHandle(owned);
            startupTrace.Stage("native_compatibility_failed_cleanup",exe,pi.dwProcessId,0,0,stopped);
            CloseHandle(pi.hProcess);
            std::puts("Native Steam VR compatibility failed or did not acknowledge within 20 seconds. Check the startup diagnostics and install the complete matching package; original game and SDK files were preserved.");
            std::puts(stopped?"The new game started by this launcher was closed safely.":"The game had already ended or could not be qualified for cleanup.");
            return startupTrace.Result(15);
        }
    }
    CloseHandle(pi.hProcess);
    if(!ok)return startupTrace.Result(7);
    ArmRecorder(package,pi.dwProcessId);
    PrintReady();return startupTrace.Result(0);
}
