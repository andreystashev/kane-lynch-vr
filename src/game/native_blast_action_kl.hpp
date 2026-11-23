#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// The game's own 0x4292A0 searches player+0x80's sorted animation-name
// dictionary, and MiniAct::Start (0x602141) resolves task+0x24 in the
// player's animation groups. Do not use a build/level-specific ordinal.
namespace NativeBlastActionKL {
constexpr char Name[]="/Level_Specific/M09_A/Cover_From_Explosives";
inline bool Address(std::uintptr_t at,std::size_t size) {
    return at>=0x10000u&&size<=0xffffffffu&&at<=0xffffffffu-size;
}
template<class Reader> bool Matches(std::uintptr_t image,std::uintptr_t player,
                                    std::uintptr_t dispatcher,std::uintptr_t action,Reader read,
                                    std::uint32_t tick=0) {
    const auto get=[&](std::uintptr_t at,auto& value){return Address(at,sizeof(value))&&read(at,&value,sizeof(value));};
    std::uint32_t vtable=0,owner=0,task=0,dict=0,count=0,current=0;
    std::uint16_t group=0;
    if(!Address(action,0x18)||!Address(player,0x84)||!Address(dispatcher,8)||
       !get(action,vtable)||vtable!=image+0x5667F4||!get(action+8,owner)||owner!=player||
       !get(action+0x14,task)||!Address(task,0x26)||!get(task+0x24,group)||group==0xffffu||
       !get(player+0x80,dict)||!get(dict,count)||!count||count>8192)return false;
    if(!Address(dict,4+static_cast<std::size_t>(count)*8))return false;
    const std::uintptr_t names=static_cast<std::uintptr_t>(dict)+4+static_cast<std::uintptr_t>(count)*8;
    if(!Address(names,sizeof(Name)))return false;
    // Per-thread cache: a negative result is valid only for the exact action,
    // task, group and dictionary, and only for the current clock tick. An
    // in-place dictionary change in that same tick can delay a positive match
    // until the next tick, never authorize a wrong animation. A found slot is
    // re-read and the full name is rechecked on every use.
    struct Cache {
        std::uintptr_t image=0,player=0,dispatcher=0,action=0,task=0;
        std::uint32_t dictionary=0,count=0,slot=0,offset=0,id=0,tick=0;
        std::uint16_t group=0;
        bool found=false,negative=false;
    };
    static thread_local Cache cache;
    if(tick&&cache.negative&&cache.tick==tick&&cache.image==image&&cache.player==player&&
       cache.dispatcher==dispatcher&&cache.action==action&&cache.task==task&&cache.group==group&&
       cache.dictionary==dict&&cache.count==count)return false;
    std::uint32_t lo=0,hi=count,matchedOffset=0,matchedId=0,matchedSlot=0;
    bool found=false;
    if(tick&&cache.found&&cache.image==image&&cache.dictionary==dict&&cache.count==count&&
       cache.slot>=dict+4&&cache.slot<names&&((cache.slot-dict-4)%8)==0) {
        std::uint32_t offset=0,id=0;char candidate[sizeof(Name)]{};
        if(get(cache.slot,offset)&&get(cache.slot+4,id)&&offset==cache.offset&&id==cache.id&&
           offset<=0x1000000u&&Address(names,static_cast<std::size_t>(offset)+sizeof(Name))&&
           read(names+offset,candidate,sizeof(candidate))&&!std::memcmp(candidate,Name,sizeof(Name))) {
            found=true;matchedOffset=offset;matchedId=id;matchedSlot=cache.slot;
        } else cache={};
    }
    while(!found&&lo<hi) {
        const auto mid=lo+(hi-lo)/2;std::uint32_t offset=0,id=0;
        const auto slot=static_cast<std::uintptr_t>(dict)+4+static_cast<std::uintptr_t>(mid)*8;
        if(!get(slot,offset)||!get(slot+4,id)||offset>0x1000000u||!Address(names,static_cast<std::size_t>(offset)+sizeof(Name)))return false;
        char candidate[sizeof(Name)]{};
        if(!read(names+offset,candidate,sizeof(candidate)))return false;
        const int cmp=std::memcmp(candidate,Name,sizeof(Name));
        if(cmp<0)lo=mid+1;
        else if(cmp>0)hi=mid;
        else {found=true;matchedOffset=offset;matchedId=id;matchedSlot=static_cast<std::uint32_t>(slot);break;}
    }
    // The native comparison returns the low word. Require the entire serialized
    // ID to be a nonnegative short, rather than trusting arbitrary high bits.
    if(!found){
        if(tick)cache={image,player,dispatcher,action,task,dict,count,0,0,0,tick,group,false,true};
        return false;
    }
    if(tick)cache={image,player,dispatcher,action,task,dict,count,matchedSlot,matchedOffset,matchedId,tick,group,true,false};
    if(matchedId!=group)return false;
    std::uint32_t after=0,afterId=0;std::uint16_t afterGroup=0;char name[sizeof(Name)]{};
    return get(dispatcher+4,current)&&current==action&&get(action,after)&&after==vtable&&
        get(action+8,after)&&after==player&&get(action+0x14,after)&&after==task&&
        get(task+0x24,afterGroup)&&afterGroup==group&&get(player+0x80,after)&&after==dict&&
        get(dict,after)&&after==count&&get(matchedSlot,after)&&after==matchedOffset&&
        get(matchedSlot+4,afterId)&&afterId==matchedId&&
        read(names+matchedOffset,name,sizeof(name))&&!std::memcmp(name,Name,sizeof(Name));
}
}
