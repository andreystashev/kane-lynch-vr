#pragma once
#include <cstdint>
#include <cmath>

namespace FrameMetrics {
// Count events actually observed, not inferred from sequence differences.
// DWORD subtraction intentionally handles a wrapping Windows tick counter.
class RateWindow {
    std::uint32_t start{}, last{};
    std::uint64_t count{};
    double rate{};
    bool initialized{};
public:
    void Observe(std::uint32_t now, unsigned events=1) {
        if(!initialized){initialized=true;start=last=now;count=0;rate=0;}
        // Retire the preceding interval before assigning events at its boundary.
        const auto elapsed=now-start;
        if(elapsed>=1000){rate=1000.0*count/elapsed;count=0;start=now;}
        count+=events;
        if(events)last=now;
    }
    double Hz(std::uint32_t now) const {
        return initialized&&now-last<2000?rate:0;
    }
};
inline std::uint32_t TickGap(std::uint32_t a,std::uint32_t b) {
    // Called on recent frames only (less than 2^31 ms apart).
    const auto forward=a-b,backward=b-a;
    return forward<backward?forward:backward;
}
}
