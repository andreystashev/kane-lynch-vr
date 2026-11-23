#pragma once
#include <cstdint>
#include <algorithm>

namespace On12QueuePolicy {
constexpr unsigned Slots=2;
constexpr std::uint64_t BudgetUs=8000;
constexpr std::uint64_t PollDelayUs=1000;
enum class Readiness {Complete,Pending,Failure};
struct Stats {
    bool enabled=false,supported=false;
    unsigned pending=0,thread=0;
    std::int32_t lastResult=0;
    std::uint64_t issued=0,completed=0,waits=0,polls=0,waitUs=0,maxWaitUs=0,budgetExhausted=0,skipped=0,failures=0,resets=0;
};
struct Queue {
    struct Entry {bool pending=false;std::uint64_t serial=0;};
    Entry entries[Slots]{};std::uint64_t serial=0;
    Stats stats{};
    unsigned Pending()const {unsigned count=0;for(const auto& slot:entries)count+=slot.pending?1u:0u;return count;}
    unsigned Available()const {for(unsigned i=0;i<Slots;++i)if(!entries[i].pending)return i;return Slots;}
    void Submitted(unsigned index){if(index>=Slots||entries[index].pending)return;entries[index]={true,++serial};++stats.issued;}
    template<class Ready,class Yield,class Clock> std::uint64_t Limit(Ready ready,Yield yield,Clock clock) {
        if(Pending()<Slots)return 0;
        unsigned oldest=0;
        for(unsigned i=1;i<Slots;++i)if(entries[i].serial<entries[oldest].serial)oldest=i;
        const auto started=clock();++stats.waits;
        for(;;){
            ++stats.polls;
            const auto state=ready(oldest);
            if(state==Readiness::Complete){entries[oldest].pending=false;++stats.completed;break;}
            if(state==Readiness::Failure){++stats.failures;break;}
            const auto elapsed=std::uint64_t(clock()-started);
            if(elapsed>=BudgetUs){++stats.budgetExhausted;break;}
            if(!yield(std::min(PollDelayUs,BudgetUs-elapsed)))break;
        }
        const auto elapsed=std::uint64_t(clock()-started);stats.waitUs+=elapsed;stats.maxWaitUs=std::max(stats.maxWaitUs,elapsed);
        return elapsed;
    }
    void Reset(){for(auto& entry:entries)entry={};serial=0;++stats.resets;}
};
}
