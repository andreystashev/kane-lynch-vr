#define WIN32_LEAN_AND_MEAN
#define OVR_CAPI_NO_UTILS
#include "recording_mirror.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
unsigned checks=0;
void Check(bool condition,const char* text){++checks;if(!condition){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);}}
DWORD Handles(){DWORD count=0;Check(GetProcessHandleCount(GetCurrentProcess(),&count)!=0,"native handle accounting");return count;}
void Request(RecordingIPC::Shared& shared,DWORD tick){
    InterlockedIncrement(&shared.request.sequence);MemoryBarrier();
    shared.request.heartbeatTick=tick;shared.request.recorderPid=GetCurrentProcessId();shared.request.eye=0;
    shared.request.fps=60;shared.request.width=1920;shared.request.height=1080;shared.request.flags=0;
    MemoryBarrier();InterlockedIncrement(&shared.request.sequence);
}
}
int main(){
    static_assert(sizeof(void*)==8,"Use the host ABI");
    Check(sizeof(RecordingIPC::Shared)==12583040,"fixed recording wire size");
    Check(offsetof(RecordingIPC::Shared,pixels)==128,"fixed pixels offset");
    Check(sizeof(RecordingMirror::MirrorDesc)==20,"modern runtime descriptor exact ABI");
    Check((RecordingMirror::LeftEyeOnly|RecordingMirror::ForceSymmetricFov)==0x42,"left rectilinear options without distortion");
    Check((RecordingMirror::RightEyeOnly|RecordingMirror::ForceSymmetricFov)==0x44,"right rectilinear options without distortion");
    RecordingIPC::Request value{};value.heartbeatTick=100;value.recorderPid=7;value.fps=60;value.width=1920;value.height=1080;
    Check(RecordingIPC::Valid(value,100),"fresh valid demand");
    Check(RecordingIPC::Valid(value,2100),"inclusive demand timeout");
    Check(!RecordingIPC::Valid(value,2101),"silent recorder stops mirror demand");
    Check(!RecordingIPC::Valid(value,99),"future heartbeat rejected");
    value.heartbeatTick=0xfffffff0u;Check(RecordingIPC::Valid(value,0x10),"heartbeat wraps safely");value.heartbeatTick=100;
    value.eye=2;Check(!RecordingIPC::Valid(value,100),"no invalid eye");value.eye=0;
    value.flags=1;Check(!RecordingIPC::Valid(value,100),"no unsupported request flags");value.flags=0;
    value.fps=121;Check(!RecordingIPC::Valid(value,100),"bounded capture rate");value.fps=60;
    value.width=2049;Check(!RecordingIPC::Valid(value,100),"width cannot exceed shared pixels");value.width=1920;
    value.height=1537;Check(!RecordingIPC::Valid(value,100),"height cannot exceed shared pixels");value.height=1080;
    RecordingIPC::Cadence cadence;unsigned due=0;
    for(unsigned n=0;n<720;++n)if(cadence.Due(100+static_cast<DWORD>(n*1000./72),60))++due;
    Check(due>=598&&due<=601,"60Hz request on72Hz host does not accidentally become36Hz");
    cadence.Reset();due=0;for(unsigned n=0;n<720;++n)if(cadence.Due(0xfffff000u+static_cast<DWORD>(n*1000./72),30))++due;
    Check(due>=298&&due<=301,"30Hz phase survives tick rollover");
    Check(!cadence.Due(1,0),"zero rate never schedules");
    wchar_t name[100];const DWORD pid=GetCurrentProcessId();RecordingIPC::MappingName(name,100,pid);
    RecordingMirror::Capture absent(pid);
    absent.Tick(nullptr,nullptr,nullptr,nullptr,100,1,1,true);
    HANDLE lookup=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
    Check(!lookup,"host never creates a recording mapping without a recorder");if(lookup)CloseHandle(lookup);
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(RecordingIPC::Shared),name);
    Check(mapping&&GetLastError()!=ERROR_ALREADY_EXISTS,"private recorder owns fresh demand channel");
    auto* shared=static_cast<RecordingIPC::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(RecordingIPC::Shared)));
    Check(shared!=nullptr,"private recorder maps production wire");std::memset(shared,0,sizeof(*shared));
    shared->magic=RecordingIPC::Magic;shared->version=RecordingIPC::Version;shared->bytes=sizeof(*shared);shared->gamePid=pid;
    Check(RecordingIPC::Compatible(shared,pid),"compatible recorder header");
    Check(!RecordingIPC::Compatible(shared,pid+1),"foreign PID rejected");
    Request(*shared,700);RecordingIPC::Request snapshot{};
    Check(RecordingIPC::ReadRequest(shared,snapshot)&&snapshot.width==1920&&snapshot.fps==60,"production seqlock reader accepts complete request");
    InterlockedIncrement(&shared->request.sequence);
    Check(!RecordingIPC::ReadRequest(shared,snapshot),"in-progress request rejected");
    InterlockedIncrement(&shared->request.sequence);
    const auto baseline=Handles();
    // A null runtime deliberately lacks the mirror exports. The production
    // adapter must report that through recording IPC and never affect VR.
    absent.Tick(nullptr,nullptr,nullptr,nullptr,700,1,2,true);
    Check(shared->response.error==static_cast<int>(RecordingIPC::Error::ExportsMissing),"missing mirror exports produce explicit recording error");
    Check(shared->response.width==0&&shared->response.height==0,"failure never masquerades as a full-layer image");
    Check(shared->response.sequence>0&&!(shared->response.sequence&1),"error response seqlock complete");
    Check(shared->response.hostPid==pid,"recording response identifies native host");
    Check(Handles()==baseline+1,"one opened mapping handle while demand active");
    Request(*shared,1300);absent.Tick(nullptr,nullptr,nullptr,nullptr,1300,2,3,true);
    Check(shared->response.sourceKind==2&&shared->response.tick==1300,"native menu state and heartbeat published");
    Request(*shared,1900);absent.Tick(nullptr,nullptr,nullptr,nullptr,1900,2,4,true);
    Check(shared->response.tick==1900,"unchanged menu state refreshes recording heartbeat");
    absent.Tick(nullptr,nullptr,nullptr,nullptr,4001,2,5,true);
    Check(Handles()==baseline,"expired demand releases opened mapping and graphics ownership");
    Request(*shared,4600);shared->response.sequence=7;
    {RecordingMirror::Capture restarted(pid);restarted.Tick(nullptr,nullptr,nullptr,nullptr,4600,1,6,true);
        Check(shared->response.sequence>0&&!(shared->response.sequence&1),"new host repairs a stale odd response sequence");
        Check(shared->response.tick==4600,"F9 replacement host publishes immediately on surviving recorder mapping");}
    shared->magic=0;shared->response.sequence=42;
    {RecordingMirror::Capture incompatible(pid);incompatible.Tick(nullptr,nullptr,nullptr,nullptr,5200,1,7,true);
        Check(shared->response.sequence==42,"incompatible channel never overwritten");}
    UnmapViewOfFile(shared);CloseHandle(mapping);
    std::printf("Recording mirror demand/ABI/cadence adapter: %u checks passed; private mappings only; no game or VR session.\n",checks);
    return 0;
}
