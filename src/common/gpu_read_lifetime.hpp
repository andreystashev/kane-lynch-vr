#pragma once
#include <cstddef>

// A shared image becomes reusable only after the completion signal for its
// last queued read has been submitted. No CPU/GPU wait is introduced here.
namespace GPUReadLifetime {
struct Completion {
    enum class Stage { Open,SignalQueued,SignalFailed,Done,Failed };
    Stage stage=Stage::Open;
    bool used=false;
    bool Open()const{return stage==Stage::Open;}
    bool NeedsFlush()const{return stage==Stage::SignalQueued||stage==Stage::SignalFailed;}
    void Queue(bool succeeded){stage=succeeded?Stage::SignalQueued:Stage::SignalFailed;}
    void FinishUnused(){if(Open()&&!used)stage=Stage::Done;}
    void Flushed(){if(NeedsFlush())stage=stage==Stage::SignalQueued?Stage::Done:Stage::Failed;}
    bool Reusable()const{return stage==Stage::Done;}
};

// Lease supplies QueueFinishRead, NeedsReadFlush, ReadContextIdentity,
// FlushReadContext and CompleteFinishRead. Keep leases alive for this call.
// Duplicate mono aliases signal once; independent eyes on one context share
// a single flush. A failed signal is flushed but can never release its slot.
template<class Lease> void FinishBatch(Lease* const* leases,std::size_t count) {
    const auto unique=[&](std::size_t i) {
        if(!leases[i])return false;
        for(std::size_t j=0;j<i;++j)if(leases[j]==leases[i])return false;
        return true;
    };
    for(std::size_t i=0;i<count;++i)if(unique(i))leases[i]->QueueFinishRead();
    for(std::size_t i=0;i<count;++i)if(unique(i)&&leases[i]->NeedsReadFlush()) {
        bool submitted=false;
        for(std::size_t j=0;j<i;++j)
            if(unique(j)&&leases[j]->NeedsReadFlush()&&
               leases[j]->ReadContextIdentity()==leases[i]->ReadContextIdentity())submitted=true;
        if(!submitted)leases[i]->FlushReadContext();
    }
    for(std::size_t i=0;i<count;++i)if(unique(i))leases[i]->CompleteFinishRead();
}
}
