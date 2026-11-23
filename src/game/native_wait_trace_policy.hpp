#pragma once
#include "native_frame_wait_kl.hpp"
#include <array>
#include <algorithm>

// Fixed total footprint/output budget. Fast unknown polling cannot evict a
// long named render/producer wait. All API calls still update site counters.
namespace NativeWaitTracePolicy {
using NativeFrameWaitKL::Event;
inline constexpr std::uint64_t SlowUs=2000;
enum Lane : unsigned {FrameSlow,FrameFast,Auxiliary,Filtered};
inline constexpr unsigned Capacity=256;
inline constexpr unsigned LaneCapacity[3]={128,64,64};
inline Lane Select(const Event& event) {
    if(event.site<=NativeFrameWaitKL::WorkerStart)
        return event.wallUs>=SlowUs?FrameSlow:FrameFast;
    if(event.site<=NativeFrameWaitKL::TaskPollSleep||event.wallUs>=SlowUs||
       (event.api!=NativeFrameWaitKL::SleepApi&&event.result==WAIT_FAILED))return Auxiliary;
    return Filtered;
}
class Ring {
    std::array<Event,Capacity> events_{};
    unsigned next_[3]{};
    std::uint64_t total_=0;
public:
    std::uint64_t Total()const{return total_;}
    bool Push(Event event) {
        const auto lane=Select(event);if(lane==Filtered)return false;
        const unsigned start=lane==FrameSlow?0:lane==FrameFast?LaneCapacity[0]:LaneCapacity[0]+LaneCapacity[1];
        event.sequence=++total_;
        events_[start+next_[lane]]=event;
        next_[lane]=(next_[lane]+1)%LaneCapacity[lane];return true;
    }
    // Call on a diagnostic-thread copy, outside the producer's lock. The
    // missing sequence count includes all lane evictions through the last
    // returned event, so partial reads do not count the same gap twice.
    std::size_t CopySince(std::uint64_t after,Event* output,std::size_t capacity,std::uint64_t& overwritten)const {
        overwritten=0;if(!output||!capacity||after>=total_)return 0;
        std::array<const Event*,Capacity> selected{};unsigned count=0;
        for(const auto& event:events_)if(event.sequence>after)selected[count++]=&event;
        std::sort(selected.begin(),selected.begin()+count,[](const Event* a,const Event* b){return a->sequence<b->sequence;});
        const auto written=std::min<std::size_t>(capacity,count);
        for(std::size_t i=0;i<written;++i)output[i]=*selected[i];
        if(written)overwritten=output[written-1].sequence-after-written;
        return written;
    }
};
}
