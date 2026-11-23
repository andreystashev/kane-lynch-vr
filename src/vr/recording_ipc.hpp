#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cwchar>
#include <cstring>

// Optional recorder-owned channel. The VR host only opens an existing mapping;
// it never creates a recorder or performs mirror/readback work without demand.
namespace RecordingIPC {
constexpr std::uint32_t Magic=0x52314C4B,Version=1;
constexpr unsigned MaximumWidth=2048,MaximumHeight=1536;
constexpr std::size_t MaximumBytes=std::size_t(MaximumWidth)*MaximumHeight*4;
constexpr DWORD RequestTimeoutMs=2000;
enum class Error : std::int32_t { None=0,ExportsMissing=-92001,MirrorFailed=-92002,
    BufferFailed=-92003,GraphicsFailed=-92004,BadRequest=-92005 };
struct Request {
    volatile LONG sequence;
    std::uint32_t heartbeatTick,recorderPid,eye,fps,width,height,flags;
};
struct Response {
    volatile LONG sequence;
    std::uint32_t hostPid,tick;
    std::int32_t error;
    // Same frame kinds as vr_ipc.hpp; zero means no submitted source yet.
    std::uint32_t sourceKind,width,height,stride;
    std::uint64_t frameId,submitId;
    // Reserved projection metadata. All zero for the runtime mirror: it is
    // ALREADY a correctly proportioned rectilinear image at requested aspect.
    // Runtime composition does not expose its projection. Never reuse the
    // submitted eye FOV here (the two projections differ) or recrop this image.
    float fov[4];
};
struct alignas(64) Shared {
    std::uint32_t magic,version,bytes,gamePid;
    Request request;
    std::uint32_t reserved[4];
    Response response;
    std::uint8_t pixels[MaximumBytes];
};
static_assert(sizeof(Request)==32&&sizeof(Response)==64,"Recorder metadata ABI");
static_assert(offsetof(Shared,request)==16&&offsetof(Shared,response)==64&&
              offsetof(Shared,pixels)==128&&sizeof(Shared)==12583040,"Recorder channel ABI");
inline void MappingName(wchar_t* out,std::size_t count,DWORD pid){
    std::swprintf(out,count,L"Local\\KaneLynchVR-Recording-%lu",pid);
}
inline bool Compatible(const Shared* shared,DWORD pid){
    return shared&&shared->magic==Magic&&shared->version==Version&&
        shared->bytes==sizeof(Shared)&&shared->gamePid==pid;
}
inline bool Valid(const Request& value,DWORD now){
    return value.heartbeatTick&&value.recorderPid&&
        static_cast<LONG>(now-value.heartbeatTick)>=0&&now-value.heartbeatTick<=RequestTimeoutMs&&
        value.eye<2&&value.fps>=1&&value.fps<=120&&value.width>=64&&value.height>=64&&
        value.width<=MaximumWidth&&value.height<=MaximumHeight&&value.flags==0;
}
inline bool ReadRequest(const Shared* shared,Request& output){
    if(!shared)return false;
    const LONG before=shared->request.sequence;
    if(!before||(before&1))return false;
    MemoryBarrier();Request copy{};
    std::memcpy(&copy,&shared->request,sizeof(copy));MemoryBarrier();
    if(shared->request.sequence!=before)return false;
    std::memcpy(&output,&copy,sizeof(output));return true;
}
inline bool Due(DWORD now,DWORD previous,unsigned fps){
    return fps&&(!previous||now-previous>=((1000u+fps-1)/fps));
}
// Preserve fractional scheduling phase. Resetting a 60Hz deadline to "now"
// on a72Hz host would incorrectly read back only every other submit (36Hz).
class Cadence {
public:
    bool Due(DWORD now,unsigned fps){
        if(!fps)return false;
        if(!armed_||fps_!=fps){armed_=true;start_=now;next_=0;fps_=fps;}
        const double elapsed=static_cast<DWORD>(now-start_);
        if(elapsed+.0001<next_)return false;
        const double interval=1000./fps;next_+=interval;
        if(next_<elapsed)next_=elapsed+interval;
        return true;
    }
    void Reset(){armed_=false;}
private:bool armed_=false;DWORD start_=0;double next_=0;unsigned fps_=0;
};
}
