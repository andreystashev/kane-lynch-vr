#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <limits>

// Status-writer telemetry only. No heap, locks, graphics calls or game hooks.
// Do not invoke from a draw hook or use a stale sample as allocation authority.
namespace LoadingMemoryHeadroom {
enum class Failure:std::uint32_t {None,InvalidBounds,QueryFailed,InvalidBase,ZeroSize,Overflow,NoProgress,UnknownState,RegionLimit};
struct Region {std::uint64_t base=0,bytes=0;std::uint32_t state=0;};
struct Bounds {std::uint64_t minimum=0,limit=0;}; // Exclusive upper bound.
struct Sample {
    bool observed=false,valid=false,availableVirtualKnown=false;
    std::uint32_t tick=0,lastError=0;
    std::uint64_t entryCount=0,scanCount=0,queryCount=0;
    std::uint64_t minimumAddress=0,limitAddress=0;
    std::uint64_t freeBytes=0,largestFreeRegion=0,committedBytes=0,reservedBytes=0;
    std::uint64_t freeRegions=0,committedRegions=0,reservedRegions=0;
    std::uint64_t availableVirtualBytes=0;
    Failure failure=Failure::None;
};
template<class Query> Sample Scan(Bounds bounds,Query&& query,std::uint64_t maximumRegions=262144){
    Sample out;out.observed=true;out.minimumAddress=bounds.minimum;out.limitAddress=bounds.limit;
    if(bounds.minimum>=bounds.limit||!maximumRegions){out.failure=Failure::InvalidBounds;return out;}
    std::uint64_t cursor=bounds.minimum;
    while(cursor<bounds.limit){
        if(out.queryCount>=maximumRegions){out.failure=Failure::RegionLimit;return out;}
        Region region;std::uint32_t error=0;++out.queryCount;
        if(!query(cursor,region,error)){out.failure=Failure::QueryFailed;out.lastError=error;return out;}
        if(region.base>cursor){out.failure=Failure::InvalidBase;return out;}
        if(!region.bytes){out.failure=Failure::ZeroSize;return out;}
        if(region.bytes>std::numeric_limits<std::uint64_t>::max()-region.base){out.failure=Failure::Overflow;return out;}
        const auto end=region.base+region.bytes;
        if(end<=cursor){out.failure=Failure::NoProgress;return out;}
        const auto clippedEnd=end<bounds.limit?end:bounds.limit;
        const auto bytes=clippedEnd-cursor;
        switch(region.state){
        case MEM_FREE:out.freeBytes+=bytes;++out.freeRegions;
            if(bytes>out.largestFreeRegion)out.largestFreeRegion=bytes;break;
        case MEM_COMMIT:out.committedBytes+=bytes;++out.committedRegions;break;
        case MEM_RESERVE:out.reservedBytes+=bytes;++out.reservedRegions;break;
        default:out.failure=Failure::UnknownState;return out;
        }
        cursor=clippedEnd;
    }
    out.valid=true;return out;
}
inline Sample ReadCurrentProcess(){
    const DWORD previousError=GetLastError();SYSTEM_INFO info{};GetSystemInfo(&info);
    const auto minimum=reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const auto maximum=reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    Sample out;
    if(std::uint64_t(maximum)==std::numeric_limits<std::uint64_t>::max()){
        out.observed=true;out.failure=Failure::InvalidBounds;
    }else{
        out=Scan({minimum,std::uint64_t(maximum)+1},[](std::uint64_t address,Region& region,std::uint32_t& error){
            if(address>std::numeric_limits<std::uintptr_t>::max()){error=ERROR_INVALID_ADDRESS;return false;}
            MEMORY_BASIC_INFORMATION memory{};
            if(VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),&memory,sizeof(memory))!=sizeof(memory)){
                error=GetLastError();return false;
            }
            region={reinterpret_cast<std::uintptr_t>(memory.BaseAddress),memory.RegionSize,memory.State};return true;
        });
    }
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    if(GlobalMemoryStatusEx(&memory)){out.availableVirtualKnown=true;out.availableVirtualBytes=memory.ullAvailVirtual;}
    SetLastError(previousError);return out;
}
class Observer {
    Sample latest_{};
public:
    static constexpr std::uint32_t RetryMs=2000;
    template<class Read> bool Observe(std::uint64_t entryCount,bool loading,std::uint32_t now,Read&& read){
        if(!loading)return false;
        if(latest_.observed&&latest_.entryCount==entryCount&&std::uint32_t(now-latest_.tick)<RetryMs)return false;
        const auto count=latest_.scanCount+1;latest_=read();latest_.observed=true;
        latest_.entryCount=entryCount;latest_.tick=now;latest_.scanCount=count;return true;
    }
    bool Observe(std::uint64_t entryCount,bool loading,std::uint32_t now){
        return Observe(entryCount,loading,now,[]{return ReadCurrentProcess();});
    }
    const Sample& Snapshot()const{return latest_;}
    void Reset(){latest_={};}
};
} // namespace LoadingMemoryHeadroom
