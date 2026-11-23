#include "gpu_read_lifetime.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
unsigned checks=0;
void Check(bool valid,const char* what){++checks;if(!valid){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
enum Event {Read,Signal,Flush,Release};
enum State {Ready,Reading,Free};
struct Context {unsigned signals=0,flushes=0,submitted=0;std::vector<Event>* events=nullptr;};
struct Slot {unsigned generation=1;State state=Reading;};
struct Lease {
    Context& context;Slot& slot;
    GPUReadLifetime::Completion completion;
    unsigned generation=1,sharedGeneration=1,signals=0,releases=0;
    bool signalSucceeds=true,cpuRequested=false,released=false;
    Lease(Context& c,Slot& s):context(c),slot(s){}
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
    ~Lease(){Lease* own=this;GPUReadLifetime::FinishBatch(&own,1);}
    void Use(){completion.used=true;if(context.events)context.events->push_back(Read);}
    void QueueFinishRead(){
        if(!completion.Open())return;
        if(!completion.used){completion.FinishUnused();return;}
        ++signals;if(context.events)context.events->push_back(Signal);
        completion.Queue(signalSucceeds);
        if(signalSucceeds)++context.signals;
        else if(sharedGeneration==generation)cpuRequested=true;
    }
    bool NeedsReadFlush()const{return completion.NeedsFlush();}
    const void* ReadContextIdentity()const{return &context;}
    void FlushReadContext(){++context.flushes;context.submitted=context.signals;if(context.events)context.events->push_back(Flush);}
    void CompleteFinishRead(){
        completion.Flushed();if(released||!completion.Reusable())return;
        released=true;++releases;
        if(completion.used)Check(context.submitted==context.signals,"successful read is submitted before the slot becomes reusable");
        if(slot.generation==generation&&slot.state==Reading)slot.state=completion.used?Free:Ready;
        if(context.events)context.events->push_back(Release);
    }
};
void Stages(){
    using Completion=GPUReadLifetime::Completion;
    Completion read;read.used=true;read.Queue(true);
    Check(read.NeedsFlush()&&!read.Reusable(),"a queued signal cannot release an unsubmitted read");
    read.Flushed();Check(read.Reusable()&&!read.NeedsFlush(),"submission completes successful read ownership");
    Completion failed;failed.used=true;failed.Queue(false);
    Check(failed.NeedsFlush()&&!failed.Reusable(),"signal failure still requires submission but never authorizes reuse");
    failed.Flushed();Check(!failed.Reusable()&&!failed.NeedsFlush()&&!failed.Open(),"failed read remains fenced off after flushing");
    Completion unused;unused.FinishUnused();
    Check(unused.Reusable()&&!unused.NeedsFlush()&&!unused.used,"unused claims return without a GPU signal or flush");
}
void StereoBatch(){
    std::vector<Event> events;Context context;context.events=&events;Slot slots[2];
    Lease left(context,slots[0]),right(context,slots[1]);left.Use();right.Use();
    // A later diagnostic read must also precede every completion signal.
    events.push_back(Read);
    Lease* leases[2]={&left,&right};GPUReadLifetime::FinishBatch(leases,2);
    Check(context.signals==2&&context.flushes==1,"independent stereo leases signal separately and share one context flush");
    Check(slots[0].state==Free&&slots[1].state==Free,"both submitted eye reads release their own slots");
    Check(events==std::vector<Event>{Read,Read,Read,Signal,Signal,Flush,Release,Release},
          "batch queues all source reads/signals before one submission and publishes releases afterwards");
    GPUReadLifetime::FinishBatch(leases,2);
    Check(context.signals==2&&context.flushes==1&&left.releases==1&&right.releases==1,
          "repeated finish/destructor fallback cannot signal or release a completed lease twice");
}
void StereoDuringUpload(){
    std::vector<Event> events;Context context;context.events=&events;Slot slots[2];
    Lease left(context,slots[0]),right(context,slots[1]);Lease* pair[2]={&left,&right};
    for(auto* lease:pair){
        lease->Use();lease->QueueFinishRead();lease->FlushReadContext();lease->CompleteFinishRead();
    }
    GPUReadLifetime::FinishBatch(pair,2);
    Check(context.signals==2&&context.flushes==2&&left.releases==1&&right.releases==1,
          "already-submitted eye completions add no batch flush after direct XR uploads");
    Check(slots[0].state==Free&&slots[1].state==Free&&
          events==std::vector<Event>{Read,Signal,Flush,Release,Read,Signal,Flush,Release},
          "each XR upload submits its own copy and completion before publishing source reuse");
}
void MonoAndUnused(){
    Context context;Slot slot;Lease mono(context,slot);mono.Use();
    Lease* aliases[3]={&mono,nullptr,&mono};GPUReadLifetime::FinishBatch(aliases,3);
    Check(mono.signals==1&&context.flushes==1&&mono.releases==1&&slot.state==Free,
          "both-eye mono aliases finish their shared lease only once");
    Slot unusedSlot;Lease unused(context,unusedSlot);Lease* own=&unused;
    GPUReadLifetime::FinishBatch(&own,1);
    Check(unused.signals==0&&context.flushes==1&&unusedSlot.state==Ready,
          "unconsumed or failed-pair claims remain Ready without another flush");
}
void PartialFailureAndContexts(){
    Context context;Slot slots[2];Lease failed(context,slots[0]),good(context,slots[1]);
    failed.signalSucceeds=false;failed.Use();good.Use();Lease* pair[2]={&failed,&good};
    GPUReadLifetime::FinishBatch(pair,2);
    Check(context.flushes==1&&failed.cpuRequested&&slots[0].state==Reading&&slots[1].state==Free,
          "one signal failure requests CPU and retains its slot while the successful eye can retire");
    GPUReadLifetime::FinishBatch(pair,2);
    Check(failed.signals==1&&good.signals==1&&context.flushes==1,
          "failed signals are not retried by destruction or a repeated batch");
    Context first,second;Slot a,b;Lease oldContext(first,a),newContext(second,b);
    oldContext.Use();newContext.Use();Lease* mixed[2]={&oldContext,&newContext};GPUReadLifetime::FinishBatch(mixed,2);
    Check(first.flushes==1&&second.flushes==1&&a.state==Free&&b.state==Free,
          "leases retained across context changes submit once on each owning context");
}
void ResetAndDestructorFallback(){
    Context context;Slot recycled;recycled.generation=2;recycled.state=Ready;
    Lease old(context,recycled);old.Use();Lease* own=&old;GPUReadLifetime::FinishBatch(&own,1);
    Check(recycled.generation==2&&recycled.state==Ready,"old generation completion cannot free a reused new-generation slot");
    Slot failedSlot;Lease failed(context,failedSlot);failed.Use();failed.signalSucceeds=false;failed.sharedGeneration=2;
    own=&failed;GPUReadLifetime::FinishBatch(&own,1);
    Check(!failed.cpuRequested&&failedSlot.state==Reading,"old generation signal failure cannot change a new generation's CPU request");
    Context fallback;Slot used;
    {Lease lease(fallback,used);lease.Use();}
    Check(fallback.signals==1&&fallback.flushes==1&&used.state==Free,"unbatched destructor preserves the original signal/flush fallback");
    Slot unused;
    {Lease lease(fallback,unused);}
    Check(fallback.signals==1&&fallback.flushes==1&&unused.state==Ready,"unused destructor returns Ready without submitting commands");
}
}
int main(){Stages();StereoBatch();StereoDuringUpload();MonoAndUnused();PartialFailureAndContexts();ResetAndDestructorFallback();
    std::printf("GPU read lifetime: %u checks PASS\n",checks);}
