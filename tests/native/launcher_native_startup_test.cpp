#include "launcher_native_startup.hpp"
#include <cstdio>
unsigned checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
int main(){
    using namespace LauncherNativeStartup;
    const auto pid=GetCurrentProcessId();const auto created=Creation(GetCurrentProcess());
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    Check(process!=nullptr,"real waitable process handle");
    Check(created!=0,"real current process creation");
    Check(Name(pid,created,false)!=Name(pid,created,true),"ready and failed are separate");
    Check(Name(pid,created,false)!=Name(pid,created+1,false),"creation scopes recycled PID");
    Check(Name(pid,created,false)!=Name(pid+1,created,false),"PID scopes channel");
    Channel owner,producer,duplicate,other;
    Check(!owner.CreateFresh(0,created),"invalid identity rejected");
    Check(owner.CreateFresh(pid,created),"launcher creates fresh exact events");
    Check(!duplicate.CreateFresh(pid,created),"duplicate cannot reuse existing events");
    Check(producer.OpenCurrent(),"DLL opens exact fresh launcher events");
    Check(!other.Open(pid,created+1),"different creation cannot open");
    Check(owner.Wait(process,2)==Outcome::TimedOut,"no acknowledgement is bounded timeout");
    Check(producer.SignalReady(),"DLL signals native ready");
    Check(owner.Wait(process,2)==Outcome::Ready,"launcher observes native ready");
    Check(!other.OpenCurrent(),"already signalled channel rejected for new producer");
    Check(producer.SignalFailed(),"DLL can report native failure");
    Check(owner.Wait(process,2)==Outcome::Failed,"failure wins over ready");
    Check(!producer.SignalReady(),"failed producer cannot later become ready");
    producer.Close();owner.Close();
    Check(!other.OpenCurrent(),"closed launcher channel unavailable");
    Check(owner.CreateFresh(pid,created),"fresh retry after all handles closed");
    Check(producer.OpenCurrent(),"fresh retry can be opened");
    Check(producer.SignalFailed(),"failure without ready");
    Check(owner.Wait(process,2)==Outcome::Failed,"launcher observes failure without ready");
    Check(owner.Wait(nullptr,2)==Outcome::Invalid,"invalid borrowed process rejected");
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Check(owner.Wait(event,2)==Outcome::Invalid,"unrelated handle cannot acknowledge game readiness");
    if(event)CloseHandle(event);producer.Close();owner.Close();
    Check(owner.CreateFresh(pid,created+1),"test stale creation channel created");
    Check(owner.Wait(process,2)==Outcome::Invalid,"process creation must match channel identity");
    if(process)CloseHandle(process);
    std::printf("Native startup events: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
