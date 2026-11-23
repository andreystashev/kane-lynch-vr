#pragma once
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include "vr_ipc.hpp"
#include "vr_mod_shared/ini.hpp"

// Release startup acknowledgement is live IPC, never an optional log/status
// file. It observes only; a failure or timeout must leave the game running.
namespace Kl1StartupStatus {
inline constexpr LONG ProfileUnavailable=-91201,ChildEnvironmentFailed=-91202,HostExitedWithoutStatus=-91203;
enum class State {Pending,Ready,NotRequested,Failed,TimedOut,GameExited};
struct Options {
    DWORD timeoutMs=30000,pollMs=20,errorStableMs=500,heartbeatFreshMs=3000;
    bool requested=true,automatic=true;
};
struct Result {
    State state=State::Pending;
    bool ready=false,observedMapping=false;
    LONG error=0,lastHostError=0;
    DWORD gamePid=0,elapsedMs=0,gameExitCode=STILL_ACTIVE;
    std::wstring detail;
};
struct Snapshot {
    bool present=false,consistent=false,compatible=false,capture=false,shutdown=false,ready=false,hmd=false;
    DWORD heartbeat=0;
    LONG error=0;
};
inline Snapshot Read(const VRIPC::SharedFrames* frames,DWORD pid){
    Snapshot out;if(!frames)return out;out.present=true;
    const auto magic=frames->magic,version=frames->version,bytes=frames->bytes,owner=frames->gamePid;
    const auto capture=frames->captureEnabled,shutdown=frames->shutdown,ready=frames->hostReady,error=frames->hostError;
    const auto heartbeat=frames->hostHeartbeatTick,hmd=frames->hostHmdPresent;
    MemoryBarrier();
    // FILE_MAP_READ: do not use InterlockedCompareExchange (RMW) on this view.
    if(magic!=frames->magic||version!=frames->version||bytes!=frames->bytes||owner!=frames->gamePid||
       capture!=frames->captureEnabled||shutdown!=frames->shutdown||ready!=frames->hostReady||error!=frames->hostError||
       heartbeat!=frames->hostHeartbeatTick||hmd!=frames->hostHmdPresent)return out;
    // A just-created, still-zero header is pending, not an ABI failure.
    if(!magic)return out;
    out.consistent=true;out.compatible=magic==VRIPC::kMagic&&version==VRIPC::kVersion&&bytes==sizeof(VRIPC::SharedFrames)&&owner==pid;
    out.capture=capture!=0;out.shutdown=shutdown!=0;out.ready=ready!=0;out.error=error;out.heartbeat=static_cast<DWORD>(heartbeat);out.hmd=hmd!=0;return out;
}
inline std::wstring Explain(LONG error){
    switch(error){
    case -1007:return L"The selected Meta connection has no headset. Connect Quest Link / Air Link, or choose Virtual Desktop / SteamVR in VR settings.";
    case -91001:return L"The installed Meta PC runtime could not be loaded. Repair Quest Link or choose another headset connection in VR settings.";
    case -91002:return L"The installed Meta runtime is missing required API exports. Repair its PC installation.";
    case -91003:return L"The headset graphics adapter could not be opened. Connect the headset to the GPU used by the game and retry.";
    case -91006:return L"The game and VR host use incompatible IPC versions. Reinstall the complete VR package.";
    case ProfileUnavailable:return L"The selected headset profile, runtime manifest or VR host is unavailable. Open VR settings, check Connection and reinstall the complete package if necessary.";
    case ChildEnvironmentFailed:return L"The selected VR host's private runtime environment could not be prepared. Restart the game and retry.";
    case HostExitedWithoutStatus:return L"The VR host exited without a live startup response. Reinstall the complete matching package or choose another headset connection. The game was left running.";
    case ERROR_REVISION_MISMATCH:return L"The live VR startup channel has an incompatible version or game identity. Reinstall the complete matching VR package.";
    default:break;
    }
    std::wstring text=L"The VR host did not start (error "+std::to_wstring(error)+L").";
    if(error>0){wchar_t* native=nullptr;const auto count=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,static_cast<DWORD>(error),MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<wchar_t*>(&native),0,nullptr);
        if(count&&native){std::wstring message(native,count);while(!message.empty()&&(message.back()==L'\r'||message.back()==L'\n'||message.back()==L' '))message.pop_back();text+=L" "+message;}if(native)LocalFree(native);}
    return text+L" Check the selected headset connection and its PC runtime. The game was left running.";
}
class Observer {
    Options options_;DWORD started_=0,errorSince_=0;LONG stableError_=0;bool timingError_=false;
    Result result_;
    Result Finish(State state,LONG error,const std::wstring& detail){result_.state=state;result_.ready=state==State::Ready;result_.error=error;result_.detail=detail;return result_;}
public:
    Observer(DWORD pid,const Options& options,DWORD started):options_(options),started_(started){result_.gamePid=pid;}
    Result Poll(const Snapshot& sample,DWORD now){
        result_.elapsedMs=DWORD(now-started_);result_.observedMapping|=sample.present;
        if(!options_.requested)return Finish(State::NotRequested,0,L"Automatic VR startup is disabled. The game was left running.");
        LONG candidate=0;
        if(sample.consistent){
            if(!sample.compatible)candidate=ERROR_REVISION_MISMATCH;
            else{
                if(sample.error)result_.lastHostError=sample.error;
                const bool fallbackPending=options_.automatic&&(sample.error==-1007||sample.error==-91001);
                if(sample.error&&!fallbackPending)candidate=sample.error;
                else if(!sample.error&&sample.shutdown)return Finish(State::NotRequested,0,L"VR output was turned off during startup. The game was left running.");
                else if(!sample.error&&sample.capture&&!sample.shutdown&&sample.ready&&sample.hmd&&sample.heartbeat&&
                    static_cast<LONG>(now-sample.heartbeat)>=0&&DWORD(now-sample.heartbeat)<=options_.heartbeatFreshMs)
                    return Finish(State::Ready,0,L"The selected VR host and connected headset are ready.");
            }
        }
        if(candidate){
            if(!timingError_||candidate!=stableError_){timingError_=true;stableError_=candidate;errorSince_=now;}
            if(DWORD(now-errorSince_)>=options_.errorStableMs)return Finish(State::Failed,candidate,Explain(candidate));
        }else{timingError_=false;stableError_=0;}
        if(result_.elapsedMs>=options_.timeoutMs){
            auto message=L"VR did not become ready within "+std::to_wstring(options_.timeoutMs/1000)+L" seconds. Connect the headset to its selected PC runtime and retry. The game was left running.";
            if(options_.automatic&&(result_.lastHostError==-1007||result_.lastHostError==-91001))message+=L" Automatic connection did not obtain a live response from its OpenXR fallback; choose the actual connection explicitly in VR settings.";
            return Finish(State::TimedOut,ERROR_TIMEOUT,message);
        }
        return result_;
    }
};
inline Options ReadOptions(const std::wstring& ini,DWORD timeoutMs=30000){
    Options options;options.timeoutMs=timeoutMs;VrModIni::File settings;std::wstring error;
    if(VrModIni::Load(ini,settings,error,true)){
        options.requested=_wtoi(VrModIni::Read(settings,L"VR",L"AutoStart",L"1").c_str())!=0;
        options.automatic=!_wcsicmp(VrModIni::Trim(VrModIni::Read(settings,L"Connection",L"Mode",L"Auto")).c_str(),L"Auto");
    }
    return options;
}
inline Result Wait(DWORD pid,const Options& options){
    const DWORD started=GetTickCount();Observer observer(pid,options,started);
    if(!options.requested)return observer.Poll({},started);
    HANDLE process=pid?OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid):nullptr;
    if(!process){Result out;out.state=State::Failed;out.error=pid?static_cast<LONG>(GetLastError()):ERROR_INVALID_PARAMETER;out.gamePid=pid;out.detail=L"Cannot observe the game process for VR startup. "+Explain(out.error);return out;}
    HANDLE mapping=nullptr;const VRIPC::SharedFrames* frames=nullptr;Result out;LONG mappingError=0;
    wchar_t name[100];VRIPC::MappingName(name,100,pid);
    for(;;){
        if(WaitForSingleObject(process,0)==WAIT_OBJECT_0){out.state=State::GameExited;out.error=ERROR_PROCESS_ABORTED;out.gamePid=pid;out.elapsedMs=GetTickCount()-started;GetExitCodeProcess(process,&out.gameExitCode);
            out.detail=L"The game exited before VR became ready (exit "+std::to_wstring(out.gameExitCode)+L"). It was not terminated by the startup check.";break;}
        if(!mapping){
            mappingError=0;
            mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
            if(mapping){frames=static_cast<const VRIPC::SharedFrames*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(VRIPC::SharedFrames)));if(!frames)mappingError=static_cast<LONG>(GetLastError());}
            else{const auto failure=GetLastError();if(failure!=ERROR_FILE_NOT_FOUND)mappingError=static_cast<LONG>(failure);}
        }
        auto sample=Read(frames,pid);
        if(mappingError){sample.present=mapping!=nullptr;sample.consistent=sample.compatible=true;sample.error=mappingError;}
        out=observer.Poll(sample,GetTickCount());
        if(out.state!=State::Pending)break;
        Sleep(std::clamp<DWORD>(options.pollMs,1,50));
    }
    if(frames)UnmapViewOfFile(frames);if(mapping)CloseHandle(mapping);CloseHandle(process);return out;
}
inline Result Wait(DWORD pid,const std::wstring& ini,DWORD timeoutMs=30000){return Wait(pid,ReadOptions(ini,timeoutMs));}
}
