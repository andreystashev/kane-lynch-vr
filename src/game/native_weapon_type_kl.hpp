#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace NativeWeaponTypeKL {
// The category is the result of template vtable+2BC, used by the actual
// player's sniper/grenade branches. +A0 is the adjacent fire-mode bitmask;
// serialized metadata offsets cannot establish this runtime member layout.
constexpr std::uintptr_t CategoryOffset=0xA4;
template<class Reader> bool Verify(std::uintptr_t image,Reader read) {
    const auto bytes=[&](std::uintptr_t rva,const unsigned char* expected,std::size_t size) {
        unsigned char actual[32]{};
        return size<=sizeof(actual)&&read(image+rva,actual,size)&&!std::memcmp(actual,expected,size);
    };
    const unsigned char getter[]={0x8B,0x81,0xA4,0,0,0,0xC3};
    const unsigned char sniper[]={0x8B,0x82,0xBC,0x02,0,0,0xFF,0xD0,0x33,0xC9,0x83,0xF8,0x0F};
    const unsigned char modes[]={0x8B,0xB6,0xA0,0,0,0};
    if(!bytes(0x279A50,getter,sizeof(getter))||!bytes(0x1EE55B,sniper,sizeof(sniper))||
       !bytes(0x120D05,modes,sizeof(modes)))return false;
    for(const auto vt:{0x577864u,0x54E6D4u}) {
        std::uint32_t method=0;
        if(!read(image+vt+0x2BC,&method,4)||method!=image+0x279A50)return false;
    }
    return true;
}
}
