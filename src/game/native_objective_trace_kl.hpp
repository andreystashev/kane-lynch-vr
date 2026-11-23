#pragma once
#include <array>
#include <cstdint>

// Bounded, opt-in diagnostics. Synchronization is supplied by the caller;
// a trace sample is evidence about a producer/draw, never render authority.
namespace NativeObjectiveTraceKL {
// A one-second status interval may contain far more than 192 producer/draw
// events. This bounded ~100 KiB snapshot covers typical bursts but does not
// promise losslessness during arbitrarily long stalls or unusually high rates;
// consumers must detect sequence gaps.
constexpr unsigned Capacity=1024;
enum class Phase:std::uint32_t { Emit=1, Draw=2 };
struct Event {
    std::uint64_t sequence=0,pair=0,producer=0;
    std::uint32_t tick=0,phase=0,stage=0,eye=2,slot=2,stamp=0;
    std::uintptr_t record=0,entity=0,geometry=0,entry=0;
    float target[3]{},clip[3]{};
    bool clipValid=false;
};
struct Snapshot {std::uint64_t written=0,overwritten=0;unsigned count=0;std::array<Event,Capacity> events{};};
static_assert(sizeof(Snapshot)<=128u*1024u,"bounded x86 status snapshot stack cost");
class Ring {
public:
    void Reset(){*this={};}
    void Push(Event event) {
        event.sequence=++written_;events_[(written_-1)%Capacity]=event;
    }
    Snapshot Copy()const {
        Snapshot out;out.written=written_;out.count=static_cast<unsigned>(written_<Capacity?written_:Capacity);
        out.overwritten=written_-out.count;
        const auto first=written_-out.count;
        for(unsigned i=0;i<out.count;++i)out.events[i]=events_[(first+i)%Capacity];
        return out;
    }
private:
    std::array<Event,Capacity> events_{};std::uint64_t written_=0;
};
}
