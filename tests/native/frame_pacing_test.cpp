#include "frame_pacing.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
}
int main(){
    FramePacing::Samples samples;
    Check(samples.Describe().p95Us==0,"empty samples have no timing");
    for(unsigned i=1;i<=100;++i)samples.Add(i);
    const auto ranks=samples.Describe();
    Check(ranks.minUs==1&&ranks.medianUs==50&&ranks.p95Us==95&&ranks.p99Us==99&&ranks.maxUs==100,
          "timings use observed nearest-rank percentiles");
    for(unsigned i=0;i<256;++i)samples.Add(7);
    Check(samples.Count()==256&&samples.Sum()==256*7&&samples.Describe().maxUs==7,
          "bounded window retires old stalls");
    FramePacing::Window pacing;
    pacing.Observe(1000,1100,2100,0);
    pacing.Observe(51000,51150,52150,10000);
    auto summary=pacing.Snapshot();
    Check(summary.interval.medianUs==50000&&summary.stalls50==1&&summary.stalls100==0,
          "start-to-start interval detects a fifty millisecond stall");
    Check(summary.lastStall.captureUs==100&&summary.lastStall.nativeWaitUs==1000,
          "a long interval is attributed to its preceding capture and Present");
    Check(summary.threadCpuPercent==20&&summary.threadCpuMeanUs==10000,
          "thread CPU work is compared with the same wall-clock interval");
    pacing.Observe(61000,121000,181000,UINT64_MAX);
    summary=pacing.Snapshot();
    Check(summary.captureStalls50==1&&summary.nativeWaitStalls50==1&&summary.cpuIntervals==1,
          "capture and native waits are distinct; unavailable CPU samples are excluded");
    pacing.Observe(181000,181100,182000);
    summary=pacing.Snapshot();
    Check(summary.lastStall.intervalUs==120000&&summary.lastStall.captureUs==60000&&
          summary.lastStall.nativeWaitUs==60000&&!summary.lastStall.cpuKnown,
          "following stall retains the actual preceding stage costs");
    pacing.Observe(10,5,15);
    Check(pacing.Snapshot().observations==0,"invalid clock order clears diagnostic state");
    std::printf("Frame pacing diagnostics: %u checks passed.\n",checks);return 0;
}
