#pragma once
#include <cstdint>
#include <cstddef>

// Automatic diagnostics share the game's 32-bit address space and GPU queue.
// They are secondary to rendering and must never compete with gameplay video.
namespace NativeVisualTracePolicy {
constexpr unsigned PreviewStride=32,IntervalMs=30000,MaxFrames=32;
constexpr std::uint64_t MinimumVirtualBytes=384ull*1024*1024;
constexpr std::uint64_t MinimumPhysicalBytes=128ull*1024*1024;
constexpr std::uint64_t EventBudget=24ull*1024*1024,DiskBudget=256ull*1024*1024;
inline bool MemoryAvailable(bool known,std::uint64_t virtualBytes,std::uint64_t physicalBytes){
    return known&&virtualBytes>=MinimumVirtualBytes&&physicalBytes>=MinimumPhysicalBytes;
}
inline bool Preview(unsigned draw){return draw&&((draw-1)%PreviewStride)==0;}
inline bool Fits(std::size_t current,std::size_t header,std::size_t payload){
    return current<=EventBudget&&header<=EventBudget-current&&payload<=EventBudget-current-header;
}
}
