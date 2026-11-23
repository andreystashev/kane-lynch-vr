#include "frame_event_trace.hpp"
#include <cstdio>
int main(){
    unsigned checks=0,failures=0;const auto check=[&](bool okay,const char* text){++checks;if(!okay){++failures;std::printf("FAIL: %s\n",text);}};
    FrameEventTrace::Ring trace;FrameEventTrace::Event e{},out[512]{};std::uint64_t lost=0;
    check(!trace.CopySince(0,out,512,lost)&&!lost,"empty trace");
    e.startUs=1000;e.thread=7;e.present=1;trace.Push(e,1100,1300,100);
    e.startUs=81000;e.present=2;trace.Push(e,85000,86000,10100);
    check(trace.CopySince(0,out,512,lost)==2&&!lost,"each Present is retained");
    check(!out[0].intervalUs&&!out[0].cpuKnown,"initial event never fabricates an interval");
    check(out[1].intervalUs==80000&&out[1].cpuUs==10000&&out[1].cpuKnown,"native stall keeps aligned thread CPU interval");
    check(out[1].previousCaptureUs==100&&out[1].previousPresentUs==200&&out[1].captureUs==4000,"stalls retain preceding and current stage costs");
    e.thread=9;e.startUs=90000;e.firstShaderUs=87000;e.shaderThread=9;e.shaderCpuUs=1500;e.shaderCpuKnown=true;
    e.firstCameraUs=86500;e.cameraThread=9;
    trace.Push(e,91000,92000,50000);
    trace.CopySince(2,out,512,lost);check(out[0].intervalUs==9000&&!out[0].cpuKnown,"worker change preserves wall gap but rejects unrelated CPU counters");
    check(out[0].shaderKnown&&out[0].preShaderKnown&&out[0].preShaderUs==1000&&out[0].shaderSpanUs==3000,
          "producer/synchronization gap is separated from the first-shader-to-Present span");
    check(out[0].shaderCpuKnown&&out[0].shaderCpuUs==1500,"render command CPU work retains its own thread qualification");
    check(out[0].cameraKnown&&out[0].firstCameraUs==86500&&out[0].cameraThread==9,"matching native camera consumer retains its exact timestamp and render thread");
    e.startUs=5;trace.Push(e,6,7,50001);trace.CopySince(3,out,512,lost);check(!out[0].intervalUs,"clock discontinuity does not underflow");
    check(!out[0].cameraKnown,"camera marker after the current Present cannot authorize a phase");
    trace.Push(e,4,7,0);check(!trace.CopySince(4,out,512,lost),"invalid stage order never creates an event");
    for(unsigned n=0;n<600;++n){e.startUs=100000+n*1000;trace.Push(e,e.startUs+10,e.startUs+20,n);}
    check(trace.CopySince(0,out,512,lost)==512&&lost==92&&out[0].sequence==93&&out[511].sequence==604,"bounded ring explicitly reports overwritten history");
    check(trace.CopySince(600,out,2,lost)==2&&out[0].sequence==601&&out[1].sequence==602&&!lost,"small drains resume without duplicates");
    check(!trace.CopySince(604,out,512,lost),"caught-up cursor does not repeat events");
    FrameEventTrace::Ring other;e.startUs=1000;e.firstCameraUs=500;e.cameraThread=7;e.thread=9;
    other.Push(e,1100,1200,0);other.CopySince(0,out,512,lost);
    check(!out[0].cameraKnown,"camera marker from another render worker is never attributed to this Present");
    e.startUs=2000;e.firstCameraUs=500;e.cameraThread=9;other.Push(e,2100,2200,0);other.CopySince(1,out,512,lost);
    check(!out[0].cameraKnown,"camera marker before the preceding Present completion cannot be reused as current setup");
    std::printf("Native frame event trace: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
