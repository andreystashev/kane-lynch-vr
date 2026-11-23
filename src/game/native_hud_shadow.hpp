#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

// A bounded copy of bytes the native dynamic-buffer writers actually filled.
// Begin invalidates before a native write; Commit runs before native Unlock.
// GPU reads require complete coverage and the same COM/device/format/size.
namespace NativeHudShadow {
enum class Kind { Vertex,Index16,Index32 };
inline bool InterfaceVertexWriter(std::uint32_t rva){return rva==0x99551||rva==0x996BF;}
inline bool CaptureWriter(Kind kind,std::uint32_t rva,std::uint32_t stride){
    // Pinned glyph/image writers and their native 16-bit quad-index builder.
    // Other writes still invalidate coverage, but never need a CPU copy.
    return kind==Kind::Vertex?stride==24&&InterfaceVertexWriter(rva):
        kind==Kind::Index16&&stride==2&&rva==0x9851C;
}
struct Identity {
    std::uintptr_t resource=0,device=0,control=0;
    std::uint32_t capacity=0;
    Kind kind=Kind::Vertex;
};
struct Allocation {
    Identity identity{};
    std::uint32_t offset=0,bytes=0;
    bool discard=false;
    std::uint32_t provenance=0;
    std::uint32_t tick=0;
};
class Cache {
public:
    static constexpr unsigned MaximumResources=6,MaximumRanges=64;
    static constexpr std::uint32_t MaximumResourceBytes=2*1024*1024;
    static constexpr std::uint32_t MaximumTotalBytes=6*1024*1024;
    bool ObserveUncaptured(std::uintptr_t control,std::uintptr_t resource,std::uint32_t capacity,
                          std::uint32_t offset,std::uint32_t bytes,bool discard){
        bool changed=false;
        for(auto& slot:slots_)if(slot.identity.resource&&
            (slot.identity.control==control||(resource&&slot.identity.resource==resource))){
            changed=true;
            if(!resource||slot.identity.resource!=resource||slot.identity.control!=control||
               slot.identity.capacity!=capacity||!bytes||std::uint64_t(offset)+bytes>capacity){slot.Reset();continue;}
            if(!++serial_)++serial_;slot.serial=serial_;
            if(discard)slot.count=0;else slot.Invalidate(offset,offset+bytes);
        }
        return changed;
    }
    std::uint64_t Begin(const Allocation& allocation) {
        const auto& id=allocation.identity;
        if(!id.resource||!id.device||!id.control||!id.capacity||id.capacity>MaximumResourceBytes||
           !allocation.bytes||std::uint64_t(allocation.offset)+allocation.bytes>id.capacity)return 0;
        Slot* slot=nullptr;
        for(auto& candidate:slots_)if(candidate.identity.resource==id.resource){slot=&candidate;break;}
        if(slot&&!Same(slot->identity,id))slot->Reset();
        if(!slot)for(auto& candidate:slots_)if(!candidate.identity.resource){slot=&candidate;break;}
        if(!slot){slot=&Oldest();slot->Reset();}
        if(!slot->identity.resource) {
            while(TotalBytes()+id.capacity>MaximumTotalBytes) {
                auto& old=Oldest();if(!old.identity.resource)return 0;old.Reset();
            }
            slot->data.resize(id.capacity);slot->identity=id;
        }
        if(!++serial_)++serial_;
        slot->serial=serial_;
        if(allocation.discard)slot->count=0;
        else slot->Invalidate(allocation.offset,allocation.offset+allocation.bytes);
        return serial_;
    }
    bool Commit(const Allocation& allocation,std::uint64_t token,const void* bytes,std::size_t count) {
        if(!token||!bytes||count!=allocation.bytes)return false;
        return Capture(allocation,token,[&](unsigned char* destination,std::size_t size){
            std::memcpy(destination,bytes,size);return true;
        });
    }
    // Call under the exclusive shadow lock. The synchronous reader must not
    // retain the destination or reenter this cache. Failed/partial native
    // reads leave no coverage, including retries of an already committed token.
    template<class Reader> bool Capture(const Allocation& allocation,std::uint64_t token,Reader reader) {
        if(!token||!allocation.bytes)return false;
        for(auto& slot:slots_)if(Same(slot.identity,allocation.identity)&&slot.serial==token) {
            if(std::uint64_t(allocation.offset)+allocation.bytes>slot.data.size())return false;
            slot.Invalidate(allocation.offset,allocation.offset+allocation.bytes);
            if(!reader(slot.data.data()+allocation.offset,allocation.bytes))return false;
            slot.Validate(allocation.offset,allocation.offset+allocation.bytes,allocation.provenance,allocation.tick);return true;
        }
        return false;
    }
    bool Read(std::uintptr_t resource,std::uintptr_t device,Kind kind,std::uint32_t capacity,
              std::uint32_t offset,std::uint32_t count,std::vector<unsigned char>& output,
              std::uint32_t* provenance=nullptr,std::uint32_t* tick=nullptr,
              std::uint32_t now=0,std::uint32_t maximumAge=0xffffffffu) const {
        if(!count||std::uint64_t(offset)+count>capacity)return false;
        for(const auto& slot:slots_) {
            if(slot.identity.resource!=resource||slot.identity.device!=device||
               slot.identity.kind!=kind||slot.identity.capacity!=capacity)continue;
            for(unsigned i=0;i<slot.count;++i)if(slot.ranges[i].begin<=offset&&
                slot.ranges[i].end>=std::uint64_t(offset)+count) {
                if(now-slot.ranges[i].tick>maximumAge)return false;
                // Reuse the caller's bounded scratch allocation. Coverage,
                // identity and age have all qualified before touching output.
                output.resize(count);
                std::memcpy(output.data(),slot.data.data()+offset,count);
                if(provenance)*provenance=slot.ranges[i].provenance;
                if(tick)*tick=slot.ranges[i].tick;return true;
            }
        }
        return false;
    }
    std::uint32_t TotalBytes() const {
        std::uint32_t total=0;for(const auto& slot:slots_)total+=slot.identity.capacity;return total;
    }
    void Clear(){for(auto& slot:slots_)slot.Reset();}
private:
    struct Range {std::uint32_t begin=0,end=0,provenance=0,tick=0;};
    struct Slot {
        Identity identity{};
        std::uint64_t serial=0;
        std::vector<unsigned char> data;
        std::array<Range,MaximumRanges> ranges{};
        unsigned count=0;
        void Reset(){identity={};serial=0;count=0;std::vector<unsigned char>().swap(data);}
        void Invalidate(std::uint32_t begin,std::uint32_t end) {
            std::array<Range,MaximumRanges> next{};unsigned size=0;bool overflow=false;
            const auto append=[&](Range range){if(size==MaximumRanges)overflow=true;else next[size++]=range;};
            for(unsigned i=0;i<count;++i) {
                const auto range=ranges[i];
                if(range.end<=begin||range.begin>=end)append(range);
                else {
                    if(range.begin<begin)append({range.begin,begin,range.provenance,range.tick});
                    if(range.end>end)append({end,range.end,range.provenance,range.tick});
                }
            }
            if(overflow){count=0;return;}
            ranges=next;count=size;
        }
        void Validate(std::uint32_t begin,std::uint32_t end,std::uint32_t provenance,std::uint32_t tick) {
            std::array<Range,MaximumRanges+1> all{};
            for(unsigned i=0;i<count;++i)all[i]=ranges[i];
            all[count]={begin,end,provenance,tick};
            std::sort(all.begin(),all.begin()+count+1,[](Range a,Range b){return a.begin<b.begin;});
            unsigned size=0;
            for(unsigned i=0;i<count+1;++i) {
                if(size&&all[i].begin<=ranges[size-1].end&&all[i].provenance==ranges[size-1].provenance) {
                    ranges[size-1].end=std::max(ranges[size-1].end,all[i].end);
                    if(static_cast<std::int32_t>(all[i].tick-ranges[size-1].tick)<0)ranges[size-1].tick=all[i].tick;
                }
                else {
                    if(size==MaximumRanges) {
                        // Retire old fragmentation without dropping the
                        // freshly observed allocation about to be drawn.
                        ranges[0]={begin,end,provenance,tick};count=1;return;
                    }
                    ranges[size++]=all[i];
                }
            }
            count=size;
        }
    };
    static bool Same(const Identity& a,const Identity& b) {
        return a.resource&&a.resource==b.resource&&a.device==b.device&&a.control==b.control&&
            a.capacity==b.capacity&&a.kind==b.kind;
    }
    Slot& Oldest() {
        Slot* result=&slots_[0];
        for(auto& slot:slots_)if(slot.identity.resource&&(!result->identity.resource||slot.serial<result->serial))result=&slot;
        return *result;
    }
    std::array<Slot,MaximumResources> slots_{};
    std::uint64_t serial_=0;
};
}
