#include "on12_queue_policy.hpp"
#include <cstdio>
#include <cstdlib>
namespace {unsigned checks=0;void Check(bool value,const char* text){++checks;if(!value){std::printf("FAIL %s\n",text);std::exit(1);}}}
int main(){
    using namespace On12QueuePolicy;Queue queue;std::uint64_t time=100;unsigned yields=0,polls=0,last=99;
    auto clock=[&]{return time;};auto yield=[&](std::uint64_t delayUs){++yields;time+=delayUs;return true;};
    auto complete=[&](unsigned slot){++polls;last=slot;return Readiness::Complete;};
    Check(queue.Available()==0&&queue.Pending()==0,"empty queue has a writable event");
    queue.Limit(complete,yield,clock);Check(!polls&&!yields&&!queue.stats.waits,"empty queue introduces no pacing");
    queue.Submitted(0);queue.Limit(complete,yield,clock);Check(!polls&&queue.Pending()==1,"one submitted frame never waits");
    queue.Submitted(1);Check(queue.Available()==Slots&&queue.Pending()==2,"two pending events cannot be overwritten");
    queue.Limit(complete,yield,clock);Check(last==0&&queue.Pending()==1&&queue.stats.completed==1&&!yields,"completed oldest frame releases without sleeping");
    queue.Submitted(0);queue.Limit(complete,yield,clock);Check(last==1&&queue.Available()==1,"FIFO order survives reuse of the first slot");
    queue.Submitted(1);const auto issued=queue.stats.issued;
    const auto wait=queue.Limit([&](unsigned){++polls;return Readiness::Pending;},yield,clock);
    Check(wait==BudgetUs&&yields==8&&queue.stats.budgetExhausted==1,"a stalled GPU has a finite eight-millisecond polling budget");
    Check(queue.Pending()==2&&queue.Available()==Slots,"budget exhaustion retains both unfinished queries");
    queue.Submitted(0);Check(queue.stats.issued==issued,"an unfinished event cannot be silently reissued");
    queue.Limit([](unsigned){return Readiness::Failure;},yield,clock);Check(queue.stats.failures==1,"driver errors are explicit, not interpreted as completion");
    queue.Reset();Check(queue.Pending()==0&&queue.stats.resets==1,"Reset retires pending state without waiting");
    queue.Submitted(0);queue.Submitted(1);time=UINT64_MAX-3000;yields=0;
    Check(queue.Limit([](unsigned){return Readiness::Pending;},yield,clock)==BudgetUs&&yields==8,"unsigned timer rollover cannot turn pacing into an unbounded wait");
    queue.Reset();queue.Submitted(0);queue.Submitted(1);yields=0;
    bool first=true;std::uint64_t delay=0;
    const auto lastWait=queue.Limit([&](unsigned){if(first){first=false;time+=BudgetUs-250;}return Readiness::Pending;},
        [&](std::uint64_t remaining){delay=remaining;return yield(remaining);},clock);
    Check(lastWait==BudgetUs&&yields==1&&delay==250,"the last timer request is clamped to the remaining budget");
    queue.Limit([](unsigned){return Readiness::Pending;},[](std::uint64_t){return false;},clock);
    Check(queue.Pending()==2&&queue.Available()==Slots,"unavailable wait timer keeps unfinished GPU work without polling continuously");
    std::printf("On12 queue policy: %u checks PASS.\n",checks);return 0;
}
