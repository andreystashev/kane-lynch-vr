#pragma once
#include <windows.h>
#include <cstdint>

namespace FrameMetrics {
inline std::uint64_t Counter() {LARGE_INTEGER tick{};QueryPerformanceCounter(&tick);return tick.QuadPart;}
inline std::uint64_t Microseconds(std::uint64_t start) {
    static const double frequency=[] {LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return double(f.QuadPart);}();
    return frequency>0?static_cast<std::uint64_t>((Counter()-start)*1000000.0/frequency):0;
}
}
