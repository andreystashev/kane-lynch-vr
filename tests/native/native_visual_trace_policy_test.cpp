#include "native_visual_trace_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace P=NativeVisualTracePolicy;
unsigned checks=0;void Check(bool ok){++checks;if(!ok)std::exit(2);}
int main(){
    Check(!P::MemoryAvailable(false,~0ull,~0ull));
    Check(P::MemoryAvailable(true,P::MinimumVirtualBytes,P::MinimumPhysicalBytes));
    Check(!P::MemoryAvailable(true,P::MinimumVirtualBytes-1,~0ull));
    Check(!P::MemoryAvailable(true,~0ull,P::MinimumPhysicalBytes-1));
    Check(P::Fits(0,24,P::EventBudget-24));Check(!P::Fits(0,24,P::EventBudget-23));
    Check(!P::Fits(P::EventBudget+1,0,0));Check(!P::Fits(std::numeric_limits<std::size_t>::max(),24,1));
    Check(!P::Fits(0,std::numeric_limits<std::size_t>::max(),1));
    Check(!P::Fits(0,24,std::numeric_limits<std::size_t>::max()));
    Check(!P::Preview(0));unsigned sampled=0;for(unsigned draw=1;draw<=8192;++draw){
        const bool want=(draw%32)==1;Check(P::Preview(draw)==want);sampled+=P::Preview(draw);
    }
    Check(sampled==256);Check(P::IntervalMs>=30000&&P::MaxFrames<=32&&P::DiskBudget<=256ull*1024*1024);
    std::printf("PASS %u automatic visual budget/memory/preview policy checks\n",checks);
}
