// Loaded before the supported bootstrap/main executable enters user code.
// Routes the game's DirectSoundCreate8 and Bink's resolved DirectSoundCreate;
// starts before entry in the exact official launcher -> game startup chain.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <atomic>
#include <string>
#include <vector>
#include "audio_bootstrap.hpp"
#include "diagnostic_session.hpp"
#include "audio_endpoint_ipc.hpp"
#include "audio_endpoint_selection.hpp"
#include "audio_route.hpp"
#include "native_render_events_kl.hpp"
#include "profile.hpp"

static_assert(sizeof(void*)==4,"Audio bootstrap belongs to the x86 game");
extern "C" __declspec(dllexport) DWORD WINAPI KaneLynchAudioInitialize(void*);
namespace {
HMODULE self=nullptr;
AudioBootstrap::Request routing;
std::wstring statusPath;
decltype(&DirectSoundCreate8) originalCreate8=nullptr;
decltype(&DirectSoundCreate) originalCreate=nullptr;
decltype(&GetProcAddress) originalBinkResolve=nullptr;
decltype(&CreateProcessW) originalProcessW=nullptr;
decltype(&CreateProcessA) originalProcessA=nullptr;
using CrtExit=void(__cdecl*)(int);
CrtExit originalCrtExit=nullptr;
std::atomic<unsigned> calls{0},redirected{0},fallbacks{0},children{0},childFailures{0};
std::atomic<unsigned> legacyCalls{0};
std::atomic<LONG> targetResult{0},fallbackResult{0};
SRWLOCK statusLock=SRWLOCK_INIT;
bool initialized=false;
bool renderEventsPrivate=false;
bool diagnosticLogging=false;
AudioEndpointIPC::Writer endpointWriter;
bool binkHooked=false;
HANDLE routeReady=nullptr,routeStop=nullptr,routeThread=nullptr;
std::atomic<bool> policyApplied{false},policyVerified{false},policyRestored{true};
std::atomic<unsigned> policyPhase{0},policySessions{0},policyTargetSessions{0};
std::atomic<LONG> policyError{0};
std::wstring routeLog;
constexpr const char* BinkSha="f1dcf44a533aa8a2e1da3a86c3af427c5d0695ccd51a800462d2812909f2b77c";

bool Same(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
std::wstring Parent(const std::wstring& path){const auto at=path.find_last_of(L"\\/");return at==std::wstring::npos?std::wstring():path.substr(0,at+1);}
bool Allowed(const wchar_t* path) {
    char digest[65]{};
    if(Same(path,routing.gamePath))return HashSupportedGameFile(path,digest);
    if(!HashFile(path,digest))return false;
    return Same(path,Parent(routing.gamePath)+L"launcher.exe")&&AudioBootstrap::IsSupportedOfficialLauncherHash(digest);
}
void Status() {
    AcquireSRWLockExclusive(&statusLock);
    wchar_t guid[64]{};StringFromGUID2(routing.endpoint,guid,64);char ascii[64]{};
    WideCharToMultiByte(CP_UTF8,0,guid,-1,ascii,64,nullptr,nullptr);
    endpointWriter.Publish(initialized,redirected.load(),targetResult.load(),ascii);
    if(!diagnosticLogging){ReleaseSRWLockExclusive(&statusLock);return;}
    const auto temporary=statusPath+L".tmp";
    if(FILE* file=_wfopen(temporary.c_str(),L"wb")) {
        std::fprintf(file,"{\"pid\":%lu,\"installed\":%s,\"renderEventsPrivate\":%s,\"renderEventsCreated\":%u,\"endpointGuid\":\"%s\",\"directSoundCalls\":%u,\"redirected\":%u,\"fallbacks\":%u,\"lastTargetHresult\":%ld,\"lastFallbackHresult\":%ld,\"childrenPrepared\":%u,\"childFailures\":%u,\"binkHooked\":%s,\"legacyDirectSoundCalls\":%u,\"policyPhase\":%u,\"policyApplied\":%s,\"policyVerified\":%s,\"policyRestored\":%s,\"policyHresult\":%ld,\"policyActiveSessions\":%u,\"policyTargetSessions\":%u}\n",
            GetCurrentProcessId(),initialized?"true":"false",renderEventsPrivate?"true":"false",
            NativeRenderEventsKL::created.load(),ascii,calls.load(),redirected.load(),fallbacks.load(),targetResult.load(),fallbackResult.load(),children.load(),childFailures.load(),binkHooked?"true":"false",legacyCalls.load(),policyPhase.load(),policyApplied?"true":"false",policyVerified?"true":"false",policyRestored?"true":"false",policyError.load(),policySessions.load(),policyTargetSessions.load());
        std::fclose(file);MoveFileExW(temporary.c_str(),statusPath.c_str(),MOVEFILE_REPLACE_EXISTING);
    }
    ReleaseSRWLockExclusive(&statusLock);
}
void RouteLog(const char* text) {
    if(!diagnosticLogging)return;
    AcquireSRWLockExclusive(&statusLock);
    if(FILE* file=_wfopen(routeLog.c_str(),L"ab")) {
        std::fprintf(file,"%lu %s\n",GetTickCount(),text);std::fclose(file);
    }
    ReleaseSRWLockExclusive(&statusLock);
}
void PolicyStatus() {
    const auto state=AudioRoute::Snapshot();policyApplied=state.applied;policyVerified=state.verified;
    policyError=state.error;policySessions=state.sessions;policyTargetSessions=state.verifiedSessions;
    policyPhase=state.verified?3u:state.applied?2u:state.state=="user_override"?6u:4u;
    Status();
}
DWORD WINAPI PolicyWorker(void*) {
    wchar_t local[MAX_PATH]{};const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
    std::wstring recovery;
    if(length&&length<MAX_PATH) {
        recovery=std::wstring(local)+L"\\KaneLynchVR";
        if(!CreateDirectoryW(recovery.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)recovery.clear();
    }
    if(recovery.empty()) {
        policyPhase=4;policyRestored=true;policyError=HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);SetEvent(routeReady);Status();return 0;
    }
    // Every policy COM call, journal operation and restore remains on this
    // one MTA worker. Native OpenAL may initialize/uninitialize COM on its
    // own audio thread; it cannot disturb the policy worker's lifetime.
    AudioRoute::Initialize(Parent(routing.modulePath),GetCurrentProcessId(),RouteLog,recovery);
    wchar_t hint[64]{};StringFromGUID2(routing.endpoint,hint,64);
    wchar_t connection[64]{};GetPrivateProfileStringW(L"Connection",L"Mode",L"Auto",connection,64,(Parent(routing.modulePath)+L"vr.ini").c_str());
    const auto backend=AudioEndpointSelection::Backend(connection);
    if(WaitForSingleObject(routeStop,0)!=WAIT_OBJECT_0)AudioRoute::Tick(hint,backend,true);
    PolicyStatus();SetEvent(routeReady);
    while(WaitForSingleObject(routeStop,2000)==WAIT_TIMEOUT) {
        AudioRoute::Tick(hint,backend,true);PolicyStatus();
    }
    policyRestored=AudioRoute::Shutdown();
    // A failed restore leaves recovery ownership in the stable journal.
    policyApplied=false;policyVerified=false;policyPhase=policyRestored?5u:4u;
    if(!policyRestored)policyError=E_FAIL;
    Status();return 0;
}
bool StartPolicy() {
    routeReady=CreateEventW(nullptr,TRUE,FALSE,nullptr);routeStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!routeReady||!routeStop) {
        const DWORD error=GetLastError();
        if(routeReady)CloseHandle(routeReady);if(routeStop)CloseHandle(routeStop);routeReady=routeStop=nullptr;
        policyPhase=4;policyError=HRESULT_FROM_WIN32(error);return false;
    }
    policyRestored=false;policyPhase=1;
    routeThread=CreateThread(nullptr,0,PolicyWorker,nullptr,0,nullptr);
    if(!routeThread) {
        const DWORD error=GetLastError();CloseHandle(routeReady);CloseHandle(routeStop);routeReady=routeStop=nullptr;
        policyRestored=true;policyPhase=4;policyError=HRESULT_FROM_WIN32(error);return false;
    }
    if(WaitForSingleObject(routeReady,7000)!=WAIT_OBJECT_0) {
        policyPhase=4;policyError=HRESULT_FROM_WIN32(ERROR_TIMEOUT);SetEvent(routeStop);Status();return false;
    }
    return policyApplied.load();
}
bool StopPolicy() {
    if(!routeThread)return true;
    SetEvent(routeStop);
    return WaitForSingleObject(routeThread,7000)==WAIT_OBJECT_0&&policyRestored.load();
}
void __cdecl Exit(int code) {
    // The pinned EXE's CRT startup calls MSVCR80!exit after WinMain.
    // This is cdecl, and runs before CRT/DLL teardown acquires loader lock.
    StopPolicy();originalCrtExit(code);
}
HRESULT WINAPI Create8(LPCGUID requested,LPDIRECTSOUND8* output,LPUNKNOWN outer) {
    ++calls;
    HRESULT result=originalCreate8(&routing.endpoint,output,outer);targetResult=result;
    if(SUCCEEDED(result))++redirected;
    else if(routing.fallback) {
        ++fallbacks;
        const GUID* fallback=requested&&IsEqualGUID(*requested,routing.endpoint)?nullptr:requested;
        result=originalCreate8(fallback,output,outer);fallbackResult=result;
    }
    Status();return result;
}
HRESULT WINAPI CreateLegacy(LPCGUID requested,LPDIRECTSOUND* output,LPUNKNOWN outer) {
    ++calls;++legacyCalls;
    HRESULT result=originalCreate(&routing.endpoint,output,outer);targetResult=result;
    if(SUCCEEDED(result))++redirected;
    else if(routing.fallback) {
        ++fallbacks;const GUID* fallback=requested&&IsEqualGUID(*requested,routing.endpoint)?nullptr:requested;
        result=originalCreate(fallback,output,outer);fallbackResult=result;
    }
    Status();return result;
}
FARPROC WINAPI BinkResolve(HMODULE module,LPCSTR name) {
    const auto resolved=originalBinkResolve(module,name);
    if(!resolved||reinterpret_cast<std::uintptr_t>(name)<0x10000||module!=GetModuleHandleW(L"dsound.dll"))return resolved;
    if(!std::strcmp(name,"DirectSoundCreate")) {
        originalCreate=reinterpret_cast<decltype(originalCreate)>(resolved);
        return reinterpret_cast<FARPROC>(reinterpret_cast<std::uintptr_t>(&CreateLegacy));
    }
    if(!std::strcmp(name,"DirectSoundCreate8"))return reinterpret_cast<FARPROC>(reinterpret_cast<std::uintptr_t>(&Create8));
    return resolved;
}
void Child(PROCESS_INFORMATION* child,DWORD flags) {
    if(!child)return;
    const DWORD error=GetLastError();wchar_t path[MAX_PATH]{};DWORD length=MAX_PATH;
    if(QueryFullProcessImageNameW(child->hProcess,0,path,&length)&&Allowed(path)) {
        if(AudioBootstrap::Attach(child->hProcess,routing))++children;else ++childFailures;
        Status();
    }
    if(!(flags&CREATE_SUSPENDED))ResumeThread(child->hThread);
    SetLastError(error);
}
BOOL WINAPI ProcessW(LPCWSTR app,LPWSTR command,LPSECURITY_ATTRIBUTES processSecurity,LPSECURITY_ATTRIBUTES threadSecurity,
                     BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION child) {
    const BOOL okay=originalProcessW(app,command,processSecurity,threadSecurity,inherit,flags|CREATE_SUSPENDED,environment,directory,startup,child);
    if(okay)Child(child,flags);return okay;
}
BOOL WINAPI ProcessA(LPCSTR app,LPSTR command,LPSECURITY_ATTRIBUTES processSecurity,LPSECURITY_ATTRIBUTES threadSecurity,
                     BOOL inherit,DWORD flags,LPVOID environment,LPCSTR directory,LPSTARTUPINFOA startup,LPPROCESS_INFORMATION child) {
    const BOOL okay=originalProcessA(app,command,processSecurity,threadSecurity,inherit,flags|CREATE_SUSPENDED,environment,directory,startup,child);
    if(okay)Child(child,flags);return okay;
}
bool Range(DWORD at,SIZE_T bytes,DWORD size){return at<size&&bytes<=size-at;}
bool Replace(void** slot,void* hook,void** original) {
    if(!*slot)return false;if(*slot==hook)return true;
    DWORD old=0;if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old))return false;
    *original=*slot;InterlockedExchangePointer(slot,hook);DWORD ignored=0;VirtualProtect(slot,sizeof(*slot),old,&ignored);return true;
}
bool Imports(HMODULE module,bool game,bool bink=false) {
    auto* base=reinterpret_cast<BYTE*>(module);const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x100000)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const DWORD size=nt->OptionalHeader.SizeOfImage;const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!Range(directory.VirtualAddress,sizeof(IMAGE_IMPORT_DESCRIPTOR),size))return false;
    struct Patch {void** slot;void* hook;void** original;void* previous;};
    std::vector<Patch> patches;
    bool sound=!game,spawn=bink,dynamic=!bink,cleanup=!game;
    for(DWORD at=directory.VirtualAddress;Range(at,sizeof(IMAGE_IMPORT_DESCRIPTOR),size);at+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* import=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+at);if(!import->Name)break;
        if(!Range(import->Name,12,size)||!import->OriginalFirstThunk||!import->FirstThunk)continue;
        const auto* library=reinterpret_cast<const char*>(base+import->Name);
        const bool dsound=game&&!_stricmp(library,"dsound.dll"),kernel=!_stricmp(library,"kernel32.dll");
        const bool runtime=game&&!_stricmp(library,"MSVCR80.dll");
        if(!dsound&&!kernel&&!runtime)continue;
        for(unsigned n=0;;++n) {
            const DWORD lookup=import->OriginalFirstThunk+n*sizeof(IMAGE_THUNK_DATA32),slot=import->FirstThunk+n*sizeof(IMAGE_THUNK_DATA32);
            if(!Range(lookup,sizeof(IMAGE_THUNK_DATA32),size)||!Range(slot,sizeof(void*),size))return false;
            const DWORD value=reinterpret_cast<const IMAGE_THUNK_DATA32*>(base+lookup)->u1.Ordinal;if(!value)break;
            void* hook=nullptr;void** original=nullptr;
            if(IMAGE_SNAP_BY_ORDINAL32(value)) {
                if(dsound&&IMAGE_ORDINAL32(value)==11){hook=reinterpret_cast<void*>(&Create8);original=reinterpret_cast<void**>(&originalCreate8);}
            } else if(Range(value,sizeof(IMAGE_IMPORT_BY_NAME)+64,size)) {
                const auto* name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+value)->Name;
                if(dsound&&!std::strcmp(name,"DirectSoundCreate8")){hook=reinterpret_cast<void*>(&Create8);original=reinterpret_cast<void**>(&originalCreate8);}
                if(kernel&&!bink&&!std::strcmp(name,"CreateProcessW")){hook=reinterpret_cast<void*>(&ProcessW);original=reinterpret_cast<void**>(&originalProcessW);}
                if(kernel&&!bink&&!std::strcmp(name,"CreateProcessA")){hook=reinterpret_cast<void*>(&ProcessA);original=reinterpret_cast<void**>(&originalProcessA);}
                if(runtime&&!std::strcmp(name,"exit")){hook=reinterpret_cast<void*>(&Exit);original=reinterpret_cast<void**>(&originalCrtExit);cleanup=true;}
                if(kernel&&bink&&!std::strcmp(name,"GetProcAddress")){hook=reinterpret_cast<void*>(&BinkResolve);original=reinterpret_cast<void**>(&originalBinkResolve);}
            }
            if(hook) {
                auto** pointer=reinterpret_cast<void**>(base+slot);
                if(!*pointer||patches.size()>=8)return false;
                patches.push_back({pointer,hook,original,*pointer});
                if(bink)dynamic=true;else if(dsound)sound=true;else spawn=true;
            }
        }
    }
    if(!sound||!spawn||!dynamic||!cleanup)return false;
    for(std::size_t i=0;i<patches.size();++i) {
        auto& patch=patches[i];
        if(Replace(patch.slot,patch.hook,patch.original))continue;
        // All required imports were located before publishing any pointer.
        // A failed publication rolls prior slots back to their exact values.
        while(i) {auto& previous=patches[--i];void* ignored=nullptr;Replace(previous.slot,previous.previous,&ignored);}
        return false;
    }
    return true;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI KaneLynchAudioInitialize(void* argument) {
    if(initialized)return 1;
    AudioBootstrap::Request request;SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),argument,&request,sizeof(request),&got)||got!=sizeof(request)||
       request.magic!=AudioBootstrap::Magic||request.version!=AudioBootstrap::Version||
       !request.gamePath[0]||request.gamePath[MAX_PATH-1]||!request.modulePath[0]||request.modulePath[MAX_PATH-1])return 0;
    wchar_t actual[MAX_PATH]{},process[MAX_PATH]{};
    if(!GetModuleFileNameW(self,actual,MAX_PATH)||!Same(actual,request.modulePath)||
       request.entryRva!=reinterpret_cast<std::uintptr_t>(&KaneLynchAudioInitialize)-reinterpret_cast<std::uintptr_t>(self)||
       !GetModuleFileNameW(nullptr,process,MAX_PATH))return 0;
    routing=request;if(!Allowed(process))return 0;
    diagnosticLogging=DiagnosticsSession::Enabled(Parent(request.modulePath));
    const bool audio=!IsEqualGUID(request.endpoint,GUID_NULL);
    if(audio)endpointWriter.Open();
    statusPath=Parent(request.modulePath)+L"KaneLynchVRAudio-"+std::to_wstring(GetCurrentProcessId())+L"-status.json";
    const bool game=Same(process,routing.gamePath);
    if(game)renderEventsPrivate=NativeRenderEventsKL::Install();
    initialized=game&&!audio?renderEventsPrivate:Imports(GetModuleHandleW(nullptr),game);
    if(initialized&&game&&audio) {
        HMODULE bink=GetModuleHandleW(L"binkw32.dll");wchar_t binkPath[MAX_PATH]{};char digest[65]{};
        if(bink&&GetModuleFileNameW(bink,binkPath,MAX_PATH)&&Same(binkPath,Parent(routing.gamePath)+L"binkw32.dll")&&
           HashFile(binkPath,digest)&&!std::strcmp(digest,BinkSha))binkHooked=Imports(bink,false,true);
        routeLog=Parent(request.modulePath)+L"KaneLynchVRAudio-"+std::to_wstring(GetCurrentProcessId())+L"-route.log";
        StartPolicy(); // Optional fail-open path; native game initialization continues.
    }
    Status();return initialized?1:0;
}
extern "C" __declspec(dllexport) DWORD WINAPI KaneLynchAudioShutdown(void*) {return StopPolicy()?1:0;}
extern "C" __declspec(dllexport) unsigned KaneLynchRenderEventsCreated() {
    return NativeRenderEventsKL::created.load(std::memory_order_relaxed);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);}return TRUE;
}
