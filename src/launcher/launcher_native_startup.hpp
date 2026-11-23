#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

// Native-hook compatibility only. Headset/host readiness is a separate concern.
// The launcher creates these fresh before loading the DLL into its new game.
namespace LauncherNativeStartup {
inline std::uint64_t Creation(HANDLE process){
    FILETIME created{},exited{},kernel{},user{};
    if(!process||!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;
    return (std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
inline std::wstring Name(DWORD pid,std::uint64_t creation,bool failed){
    if(!pid||!creation)return {};
    return L"Local\\KaneLynchVRStartup-v1-"+std::to_wstring(pid)+L"-"+
        std::to_wstring(creation)+(failed?L"-Failed":L"-Ready");
}
enum class Outcome {Ready,Failed,GameExited,TimedOut,Invalid};
class Channel {
    HANDLE ready_=nullptr,failed_=nullptr;
    DWORD pid_=0;std::uint64_t creation_=0;
public:
    Channel()=default;Channel(const Channel&)=delete;Channel& operator=(const Channel&)=delete;
    ~Channel(){Close();}
    void Close(){if(ready_)CloseHandle(ready_);if(failed_)CloseHandle(failed_);ready_=failed_=nullptr;pid_=0;creation_=0;}
    bool Valid()const{return ready_&&failed_;}
    bool CreateFresh(DWORD pid,std::uint64_t creation){
        Close();const auto ready=Name(pid,creation,false),failed=Name(pid,creation,true);
        if(ready.empty())return false;
        ready_=CreateEventW(nullptr,TRUE,FALSE,ready.c_str());
        if(!ready_||GetLastError()==ERROR_ALREADY_EXISTS){Close();return false;}
        failed_=CreateEventW(nullptr,TRUE,FALSE,failed.c_str());
        if(!failed_||GetLastError()==ERROR_ALREADY_EXISTS){Close();return false;}
        pid_=pid;creation_=creation;return true;
    }
    bool Open(DWORD pid,std::uint64_t creation){
        Close();const auto ready=Name(pid,creation,false),failed=Name(pid,creation,true);
        if(ready.empty())return false;
        ready_=OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,ready.c_str());
        failed_=OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,failed.c_str());
        if(!Valid()||WaitForSingleObject(ready_,0)!=WAIT_TIMEOUT||
           WaitForSingleObject(failed_,0)!=WAIT_TIMEOUT){Close();return false;}
        pid_=pid;creation_=creation;return true;
    }
    bool OpenCurrent(){return Open(GetCurrentProcessId(),Creation(GetCurrentProcess()));}
    bool SignalReady(){return Valid()&&WaitForSingleObject(failed_,0)==WAIT_TIMEOUT&&SetEvent(ready_);}
    bool SignalFailed(){return Valid()&&SetEvent(failed_);}
    Outcome Wait(HANDLE game,DWORD timeoutMs=20000)const{
        if(!Valid()||!game||GetProcessId(game)!=pid_||Creation(game)!=creation_||
           !timeoutMs||timeoutMs>60000)return Outcome::Invalid;
        // Failure has priority even if a faulty producer signalled both events.
        const HANDLE handles[]={failed_,game,ready_};
        const DWORD result=WaitForMultipleObjects(3,handles,FALSE,timeoutMs);
        if(WaitForSingleObject(failed_,0)==WAIT_OBJECT_0)return Outcome::Failed;
        if(WaitForSingleObject(game,0)==WAIT_OBJECT_0)return Outcome::GameExited;
        if(result==WAIT_OBJECT_0+2&&WaitForSingleObject(ready_,0)==WAIT_OBJECT_0)return Outcome::Ready;
        return result==WAIT_TIMEOUT?Outcome::TimedOut:Outcome::Invalid;
    }
};
} // namespace LauncherNativeStartup
