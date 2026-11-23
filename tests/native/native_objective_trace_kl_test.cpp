#include "native_objective_trace_kl.hpp"
#include <cstdio>
#include <cstdlib>
namespace {unsigned checks=0;void Check(bool ok,const char* text){++checks;if(!ok){std::printf("FAIL: %s\n",text);std::exit(1);}}}
int main(){
    using namespace NativeObjectiveTraceKL;
    Ring ring;auto empty=ring.Copy();Check(empty.written==0&&empty.count==0&&empty.overwritten==0,"empty");
    Check(sizeof(Snapshot)<=128u*1024u&&sizeof(Ring)<=128u*1024u,"snapshot and ring memory remain bounded");
    for(unsigned i=0;i<Capacity+7;++i){Event event;event.tick=i+1;event.pair=i/2+1;event.eye=i&1;event.stage=i%3;event.producer=i+100;event.entry=0x10000+i;event.slot=i&1;event.stamp=i+20;
        event.phase=static_cast<unsigned>(Phase::Draw);event.target[0]=float(i);ring.Push(event);
        if(i==Capacity-2){const auto near=ring.Copy();Check(near.written==Capacity-1&&near.count==Capacity-1&&!near.overwritten&&near.events[0].sequence==1&&near.events[Capacity-2].sequence==Capacity-1,"before capacity no lost events");}
        if(i==Capacity-1){const auto exact=ring.Copy();Check(exact.written==Capacity&&exact.count==Capacity&&!exact.overwritten&&exact.events[0].sequence==1&&exact.events[Capacity-1].sequence==Capacity,"exact capacity no lost events");}}
    auto full=ring.Copy();Check(full.written==Capacity+7&&full.count==Capacity&&full.overwritten==7,"bounded overwrite accounting");
    for(unsigned i=0;i<Capacity;++i){const auto& event=full.events[i];Check(event.sequence==i+8&&event.tick==i+8&&event.target[0]==float(i+7),"chronological wrap");}
    Check(full.events[0].eye==1&&full.events[1].eye==0,"preserves pair eye independently");
    Check(full.events[0].producer==107&&full.events[0].entry==0x10007&&full.events[0].slot==1&&full.events[0].stamp==27,"packet identity preserved");
    ring.Reset();auto reset=ring.Copy();Check(!reset.written&&!reset.count&&!reset.overwritten,"reset");
    std::printf("Objective opt-in bounded event trace: %u checks PASS; Event=%zu, Ring=%zu, Snapshot=%zu bytes.\n",checks,sizeof(Event),sizeof(Ring),sizeof(Snapshot));return 0;
}
