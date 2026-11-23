#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// Bounded wall-clock measurements. Collection never allocates or sorts;
// callers protect the window and sort a copy on the diagnostics thread.
namespace FramePacing {
struct Distribution {std::uint64_t minUs=0,medianUs=0,p95Us=0,p99Us=0,maxUs=0;};
class Samples {
    std::array<std::uint64_t,256> values{};
    unsigned count=0,next=0;
public:
    void Add(std::uint64_t us){values[next]=us;next=(next+1)%values.size();count=std::min(unsigned(values.size()),count+1);}
    unsigned Count()const{return count;}
    std::uint64_t Sum()const{std::uint64_t sum=0;for(unsigned i=0;i<count;++i)sum+=values[i];return sum;}
    Distribution Describe()const{
        if(!count)return {};auto sorted=values;std::sort(sorted.begin(),sorted.begin()+count);
        const auto rank=[&](unsigned percent){return sorted[(count*percent+99)/100-1];};
        return {sorted[0],rank(50),rank(95),rank(99),sorted[count-1]};
    }
};
struct Stall {std::uint64_t observation=0,intervalUs=0,cpuUs=0,captureUs=0,nativeWaitUs=0;bool cpuKnown=false;};
struct Summary {unsigned count=0;Distribution interval,capture,nativeWait;unsigned cpuIntervals=0;double threadCpuPercent=0,threadCpuMeanUs=0;
    std::uint64_t observations=0,stalls50=0,stalls100=0,stalls250=0,captureStalls50=0,nativeWaitStalls50=0;
    Stall lastStall{},worstStall{};};
class Window {
    Samples intervals,captures,waits;
    Samples cpuWork,cpuWall;
    std::uint64_t previousCpu=UINT64_MAX;
    std::uint64_t previous=0;bool initialized=false;
    std::uint64_t observations=0,stalls50=0,stalls100=0,stalls250=0,captureStalls50=0,nativeWaitStalls50=0;
    std::uint64_t previousCapture=0,previousWait=0;Stall lastStall{},worstStall{};
public:
    void Observe(std::uint64_t start,std::uint64_t nativeStart,std::uint64_t end,std::uint64_t cpu=UINT64_MAX){
        if(nativeStart<start||end<nativeStart){*this={};return;}
        if(initialized&&start>=previous){
            const auto interval=start-previous;intervals.Add(interval);
            if(interval>=50000){
                ++stalls50;if(interval>=100000)++stalls100;if(interval>=250000)++stalls250;
                const bool known=cpu!=UINT64_MAX&&previousCpu!=UINT64_MAX&&cpu>=previousCpu;
                // start-to-start belongs to the preceding capture/Present,
                // not the current frame's yet-to-be-accounted native wait.
                lastStall={observations+1,interval,known?cpu-previousCpu:0,previousCapture,previousWait,known};
                if(interval>worstStall.intervalUs)worstStall=lastStall;
            }
            if(cpu!=UINT64_MAX&&previousCpu!=UINT64_MAX&&cpu>=previousCpu){cpuWork.Add(cpu-previousCpu);cpuWall.Add(start-previous);}
        }
        else if(initialized)*this={};
        previous=start;previousCpu=cpu;initialized=true;previousCapture=nativeStart-start;previousWait=end-nativeStart;
        ++observations;if(previousCapture>=50000)++captureStalls50;if(previousWait>=50000)++nativeWaitStalls50;
        captures.Add(previousCapture);waits.Add(previousWait);
    }
    Summary Snapshot()const{auto result=Summary{waits.Count(),intervals.Describe(),captures.Describe(),waits.Describe()};
        result.cpuIntervals=cpuWork.Count();const auto wall=cpuWall.Sum(),cpu=cpuWork.Sum();
        result.threadCpuPercent=wall?100.*double(cpu)/wall:0;result.threadCpuMeanUs=cpuWork.Count()?double(cpu)/cpuWork.Count():0;
        result.observations=observations;result.stalls50=stalls50;result.stalls100=stalls100;result.stalls250=stalls250;
        result.captureStalls50=captureStalls50;result.nativeWaitStalls50=nativeWaitStalls50;result.lastStall=lastStall;result.worstStall=worstStall;return result;}
};
}
