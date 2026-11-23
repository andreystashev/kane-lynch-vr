#pragma once
#include <cstdint>
#include <cstring>

namespace NativeCameraMoverKL {
// player+9AC is the current input mover, not the ZFFCamera1 renderer.
// Native 5E75D0/5F9853 selects +9A4 (gamepad) or +9A8 (mouse).
constexpr std::uintptr_t MouseVtable=0x56AD84,GamepadVtable=0x56AE50;
inline bool KnownVtable(std::uintptr_t image,std::uintptr_t value) {
    return value==image+MouseVtable||value==image+GamepadVtable;
}
template<class Reader> bool VerifyImage(std::uintptr_t image,Reader read) {
    const auto bytes=[&](std::uintptr_t rva,const unsigned char* expected,std::size_t count) {
        unsigned char actual[32]{};
        return count<=sizeof(actual)&&read(image+rva,actual,count)&&!std::memcmp(actual,expected,count);
    };
    const unsigned char getter[]={0x8B,0x81,0xAC,9,0,0,0xC3};
    const unsigned char gamepadSelection[]={0x8B,0x86,0xA4,9,0,0,0x89,0x86,0xAC,9,0,0};
    const unsigned char mouseSelection[]={0x8B,0x8E,0xA8,9,0,0,0x89,0x8E,0xAC,9,0,0};
    const unsigned char ownerStore[]={0xD9,0xEE,0x8B,0x44,0x24,4,0x53,0x33,0xDB,0x56,0x8B,0xF1,0x89,0x46,8};
    const unsigned char store[]={0xC7,0x06};
    std::uint32_t mouse=0,gamepad=0;
    return bytes(0x1E7910,getter,sizeof(getter))&&bytes(0x1E75F4,gamepadSelection,sizeof(gamepadSelection))&&
        bytes(0x1E7602,mouseSelection,sizeof(mouseSelection))&&bytes(0x22FE80,ownerStore,sizeof(ownerStore))&&
        bytes(0x2338B0,store,sizeof(store))&&read(image+0x2338B2,&gamepad,4)&&gamepad==image+GamepadVtable&&
        bytes(0x233495,store,sizeof(store))&&read(image+0x233497,&mouse,4)&&mouse==image+MouseVtable;
}
} // namespace NativeCameraMoverKL
