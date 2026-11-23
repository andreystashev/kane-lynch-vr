#pragma once
#include "cpu_timer.hpp"
#include <array>
#include <algorithm>

namespace HostFrameTiming {
enum Stage {TransportRead,WaitFrame,AcquireImage,WaitImage,FlushCopy,EndFrame,StatusWrite,BeginFrame,TrackingSample,ReleaseImage,Count};
inline constexpr const char* Names[]={"transportRead","xrWaitFrame","xrAcquireImage","xrWaitImage","xrFlushCopy","xrEndFrame","statusWrite","xrBeginFrame","xrTrackingSample","xrReleaseImage"};
struct StageStats {
    std::uint64_t calls=0,maxUs=0,slowCalls=0,lastSlowUs=0;
    DWORD lastSlowTick=0;
};
struct EndFrameBurst {
    DWORD firstTick=0,lastTick=0;
    std::uint64_t calls=0,layeredCalls=0,totalUs=0,maxUs=0;
};
inline constexpr std::uint64_t slowUs=50000;
inline EndFrameBurst activeEndFrameBurst{},lastEndFrameBurst{};
inline void ObserveEndFrame(std::uint64_t elapsed,DWORD now,bool hasLayers) {
    auto& burst=activeEndFrameBurst;
    if(elapsed<slowUs){
        if(burst.calls){lastEndFrameBurst=burst;burst={};}
        return;
    }
    if(!burst.calls)burst.firstTick=now-DWORD(elapsed/1000);
    burst.lastTick=now;
    ++burst.calls;
    burst.layeredCalls+=hasLayers;
    burst.totalUs+=elapsed;
    burst.maxUs=std::max(burst.maxUs,elapsed);
}
inline std::array<StageStats,Count> stats{};
inline bool enabled=false;
struct Scope {
    Stage stage;
    std::uint64_t start;
    bool hasLayers;
    explicit Scope(Stage next,bool layered=false):stage(next),start(enabled?FrameMetrics::Counter():0),hasLayers(layered){}
    ~Scope(){
        if(!start)return;
        const auto elapsed=FrameMetrics::Microseconds(start);
        auto& sample=stats[stage];++sample.calls;sample.maxUs=std::max(sample.maxUs,elapsed);
        if(elapsed<slowUs){
            if(stage==EndFrame)ObserveEndFrame(elapsed,0,hasLayers);
            return;
        }
        ++sample.slowCalls;sample.lastSlowUs=elapsed;sample.lastSlowTick=GetTickCount();
        if(stage==EndFrame)ObserveEndFrame(elapsed,sample.lastSlowTick,hasLayers);
    }
};
}
