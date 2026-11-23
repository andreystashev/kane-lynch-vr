#pragma once
#include <array>
#include <algorithm>
#include <cstdint>

// Every native Present, retained without allocations or render-thread I/O.
// The diagnostics worker drains new events into the rotating session JSONL.
namespace FrameEventTrace {
struct Event {
    std::uint64_t sequence=0,present=0,startUs=0,intervalUs=0,cpuUs=0,captureUs=0,nativePresentUs=0;
    std::uint64_t previousCaptureUs=0,previousPresentUs=0,captured=0,drops=0,failures=0,sourceFrameId=0;
    std::uint32_t tick=0,thread=0,eye=0,kind=0,gate=0;
    std::int32_t captureResult=0,presentResult=0;
    bool cpuKnown=false;
    std::uint64_t firstShaderUs=0,shaderSpanUs=0,shaderCpuUs=0,preShaderUs=0;
    std::uint32_t shaderThread=0;
    bool shaderKnown=false,shaderCpuKnown=false,preShaderKnown=false;
    std::uint64_t firstCameraUs=0;
    std::uint32_t cameraThread=0;
    bool cameraKnown=false;
};
class Ring {
    std::array<Event,512> events{};
    std::uint64_t total=0,previousStart=0,previousCpu=UINT64_MAX,previousCapture=0,previousPresent=0;
    std::uint32_t previousThread=0;
    bool initialized=false;
public:
    void Push(Event event,std::uint64_t nativeStart,std::uint64_t end,std::uint64_t cpu) {
        if(nativeStart<event.startUs||end<nativeStart)return;
        event.sequence=++total;
        event.captureUs=nativeStart-event.startUs;event.nativePresentUs=end-nativeStart;
        event.cameraKnown=event.firstCameraUs&&event.firstCameraUs<=event.startUs&&event.cameraThread==event.thread&&
            (!initialized||event.firstCameraUs>=previousStart+previousCapture+previousPresent);
        if(event.firstShaderUs&&event.firstShaderUs<=event.startUs){
            event.shaderKnown=true;event.shaderSpanUs=event.startUs-event.firstShaderUs;
            const auto previousEnd=previousStart+previousCapture+previousPresent;
            if(initialized&&event.firstShaderUs>=previousEnd){event.preShaderKnown=true;event.preShaderUs=event.firstShaderUs-previousEnd;}
        }
        if(initialized&&event.startUs>=previousStart) {
            event.intervalUs=event.startUs-previousStart;
            event.previousCaptureUs=previousCapture;event.previousPresentUs=previousPresent;
            event.cpuKnown=event.thread==previousThread&&cpu!=UINT64_MAX&&previousCpu!=UINT64_MAX&&cpu>=previousCpu;
            if(event.cpuKnown)event.cpuUs=cpu-previousCpu;
        }
        events[(total-1)%events.size()]=event;
        previousStart=event.startUs;previousCpu=cpu;previousThread=event.thread;
        previousCapture=event.captureUs;previousPresent=event.nativePresentUs;initialized=true;
    }
    std::size_t CopySince(std::uint64_t after,Event* output,std::size_t capacity,std::uint64_t& overwritten)const {
        overwritten=0;if(!output||!capacity||after>=total)return 0;
        const auto oldest=total>events.size()?total-events.size()+1:1;
        const auto first=std::max(after+1,oldest);
        overwritten=first-(after+1);
        const auto count=std::min<std::uint64_t>(capacity,total-first+1);
        for(std::uint64_t i=0;i<count;++i)output[i]=events[(first+i-1)%events.size()];
        return static_cast<std::size_t>(count);
    }
};
}
