#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cwchar>
#include <type_traits>

// A separate, pointer-free channel keeps the image ABI independent of hands.
// Both head and wrists come from ONE predicted LibOVR tracking state.
namespace HandsIPC {
constexpr std::uint32_t kMagic=0x324C4B56,kVersion=3;
constexpr DWORD kFreshMs=100;
constexpr std::uint32_t kOrientation=1,kPosition=2;
constexpr std::uint32_t kGripInput=1,kTriggerInput=2;
#pragma pack(push,8)
struct Pose {
    float orientation[4]{}; // LibOVR x,y,z,w; device -> tracking
    float position[3]{};    // metres, same tracking origin as image metadata
    std::uint32_t flags=0;
};
struct Frame {
    Pose head{},hands[2]{};
    float grip[2]{},trigger[2]{};
    std::uint32_t tick=0,recenterAck=0,present=0,mounted=0;
    double sensorTime=0;
    // Same predicted tracking sample as the poses; m/s in tracking space.
    float linearVelocity[2][3]{};
    std::uint32_t velocityValid[2]{};
    // Input availability is separate from tracked pose availability. An
    // inactive OpenXR action must not look like a physical grip release.
    std::uint32_t inputValid[2]{};
};
struct alignas(64) Shared {
    std::uint32_t magic,version,bytes,gamePid;
    volatile LONG sequence,hostPid,heartbeat,shutdown;
    Frame frame;
};
#pragma pack(pop)
static_assert(sizeof(Pose)==32&&sizeof(Frame)==176,"Hands IPC payload changed");
static_assert(offsetof(Shared,frame)==32&&sizeof(Shared)==256,"Hands IPC header changed");
static_assert(std::is_trivially_copyable<Frame>::value,"Hands IPC must be copyable");
inline void Name(wchar_t* name,std::size_t count,DWORD pid) {
    std::swprintf(name,count,L"Local\\KaneLynchVRHands-%lu",pid);
}
inline bool Compatible(const Shared& shared,DWORD pid) {
    return shared.magic==kMagic&&shared.version==kVersion&&shared.bytes==sizeof(Shared)&&shared.gamePid==pid;
}
inline bool Fresh(DWORD now,DWORD tick) {
    return tick&&static_cast<LONG>(now-tick)>=0&&now-tick<=kFreshMs;
}
class Producer {
    HANDLE mapping_=nullptr;
    Shared* shared_=nullptr;
public:
    Producer()=default;
    Producer(const Producer&)=delete;
    Producer& operator=(const Producer&)=delete;
    ~Producer(){Close();}
    bool Ready()const{return shared_&&shared_->hostPid==static_cast<LONG>(GetCurrentProcessId())&&!shared_->shutdown;}
    void Close() {
        if(shared_) {
            InterlockedExchange(&shared_->shutdown,1);
            InterlockedExchange(&shared_->hostPid,0);
            UnmapViewOfFile(shared_);shared_=nullptr;
        }
        if(mapping_){CloseHandle(mapping_);mapping_=nullptr;}
    }
    bool Open(DWORD pid) {
        Close();wchar_t name[96];Name(name,96,pid);
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name);
        if(!mapping_)return false;
        auto* shared=static_cast<Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
        if(!shared){CloseHandle(mapping_);mapping_=nullptr;return false;}
        const LONG self=static_cast<LONG>(GetCurrentProcessId());
        LONG owner=InterlockedCompareExchange(&shared->hostPid,0,0);
        if(owner) {
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(owner));
            const bool stopped=process?WaitForSingleObject(process,0)==WAIT_OBJECT_0:GetLastError()==ERROR_INVALID_PARAMETER;
            if(process)CloseHandle(process);
            if(!stopped){UnmapViewOfFile(shared);CloseHandle(mapping_);mapping_=nullptr;return false;}
        }
        if(InterlockedCompareExchange(&shared->hostPid,self,owner)!=owner) {
            UnmapViewOfFile(shared);CloseHandle(mapping_);mapping_=nullptr;return false;
        }
        shared_=shared;
        InterlockedExchange(&shared_->sequence,1);
        shared_->magic=kMagic;shared_->version=kVersion;shared_->bytes=sizeof(Shared);shared_->gamePid=pid;
        shared_->frame={};InterlockedExchange(&shared_->heartbeat,0);InterlockedExchange(&shared_->shutdown,0);
        MemoryBarrier();InterlockedExchange(&shared_->sequence,2);return true;
    }
    void Publish(const Frame& frame) {
        if(!shared_)return;
        const LONG sequence=InterlockedCompareExchange(&shared_->sequence,0,0);
        InterlockedExchange(&shared_->sequence,sequence+1);MemoryBarrier();
        shared_->frame=frame;MemoryBarrier();
        InterlockedExchange(&shared_->sequence,sequence+2);
        InterlockedExchange(&shared_->heartbeat,static_cast<LONG>(frame.tick));
    }
};
class Consumer {
    HANDLE mapping_=nullptr;
    const Shared* shared_=nullptr;
    DWORD pid_=0;
public:
    Consumer()=default;
    Consumer(const Consumer&)=delete;
    Consumer& operator=(const Consumer&)=delete;
    ~Consumer(){Close();}
    void Close() {
        if(shared_){UnmapViewOfFile(shared_);shared_=nullptr;}
        if(mapping_){CloseHandle(mapping_);mapping_=nullptr;}pid_=0;
    }
    bool Open(DWORD pid) {
        if(shared_&&pid_==pid)return true;
        Close();wchar_t name[96];Name(name,96,pid);
        mapping_=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
        if(!mapping_)return false;
        shared_=static_cast<const Shared*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,sizeof(Shared)));
        if(!shared_){Close();return false;}pid_=pid;return true;
    }
    bool Read(Frame& output,DWORD now) const {
        if(!shared_)return false;
        for(unsigned retry=0;retry<3;++retry) {
            const LONG before=shared_->sequence;MemoryBarrier();
            if(!before||(before&1))continue;
            if(!Compatible(*shared_,pid_)||!shared_->hostPid||shared_->shutdown)return false;
            const Frame copy=shared_->frame;MemoryBarrier();
            if(before==shared_->sequence) {
                if(!Fresh(now,copy.tick)||(copy.inputValid[0]&~3u)||(copy.inputValid[1]&~3u))return false;
                output=copy;return true;
            }
        }
        return false;
    }
};
} // namespace HandsIPC
