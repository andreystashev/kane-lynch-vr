#pragma once
#include <cstdint>
#include <cstddef>

namespace NativeSmokeLoopKL {
constexpr std::uintptr_t Event=0x296C20,Cleanup=0x296FE0,Lookup=0x54E90,Fade=0x10ECA0;
struct Replacement {
    std::uint32_t effect=0,vtable=0,entity=0,application=0,database=0,audio=0,definition=0,handle=0;
    unsigned slot=0;
};
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto hash=[&](std::uintptr_t rva,unsigned count,std::uint64_t expected){
        unsigned char bytes[0x300]{};if(count>sizeof(bytes)||!read(image+rva,bytes,count))return false;
        std::uint64_t value=14695981039346656037ull;
        for(unsigned i=0;i<count;++i){value^=bytes[i];value*=1099511628211ull;}return value==expected;
    };
    // Whole native event, sound lookup and stop bodies, not a guessed vtable
    // slot. All supported images retain this fixed-base native code layout.
    return image==0x400000&&hash(Event,0x2FC,0x1ADFD7B326FE6771ull)&&
        hash(Cleanup,0x164,0x9305D79C7C90ADC4ull)&&
        hash(Lookup,0x23,0x33E48EFD71CA32E9ull)&&hash(Fade,0x19E,0x4FCB8A8A4CDC198Aull);
}
template<class Reader> bool Capture(std::uintptr_t image,std::uintptr_t effect,std::uint32_t event,
                                    Reader read,Replacement& output) {
    Replacement next;std::uint16_t start=0;std::uint32_t phase=0;
    if(effect<0x10000||effect>0xFFFFFFFFu-0x98||
       !read(effect,&next.vtable,4)||(next.vtable!=image+0x57ADCC&&next.vtable!=image+0x57AE5C)||
       !read(effect+0x24,&next.entity,4)||next.entity<0x10000||
       !read(effect+0x3A,&start,2)||!read(image+0x88B0BC,&phase,4)||
       !read(image+0x6875F0,&next.application,4)||next.application<0x10000||
       !read(next.application+0xA8,&next.database,4)||
       !read(next.application+0xAFC,&next.audio,4)||!next.audio)return false;
    // Match the original priority and low-word event dispatch exactly. The
    // other smoke phase, stop and position/reset events remain untouched.
    const auto id=std::uint16_t(event);
    if(id==start)next.slot=0x4C;
    else if(phase&&id==phase)next.slot=0x50;
    else return false;
    const auto definition=next.slot==0x4C?0x44:0x40;
    if(!read(effect+definition,&next.definition,4)||!next.definition||
       !read(effect+next.slot,&next.handle,4)||!next.handle)return false;
    next.effect=std::uint32_t(effect);output=next;return true;
}
inline bool Same(const Replacement& a,const Replacement& b) {
    return a.effect==b.effect&&a.vtable==b.vtable&&a.entity==b.entity&&a.application==b.application&&
        a.database==b.database&&a.audio==b.audio&&a.definition==b.definition&&a.handle==b.handle&&a.slot==b.slot;
}
}
